/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"
#include "core-pthread.h"

#if defined(HAVE_MALLOC_H)
#include <malloc.h>
#endif

#define MIN_MALLOC_BYTES	(1 * KB)
#define MAX_MALLOC_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_MALLOC_BYTES	(64 * KB)

#define MIN_MALLOC_MAX		(32)
#define MAX_MALLOC_MAX		(256 * 1024)
#define DEFAULT_MALLOC_MAX	(64 * KB)

#define MIN_MALLOC_THRESHOLD	(1)
#define MAX_MALLOC_THRESHOLD	(256 * MB)
#define DEFAULT_MALLOC_THRESHOLD (128 * KB)

#define MIN_MALLOC_PTHREADS	(0)
#define MAX_MALLOC_PTHREADS	(32)
#define DEFAULT_MALLOC_PTHREADS	(0)

#define MK_ALIGN(x)	(1U << (3 + ((x) & 7)))

typedef struct {
	uintptr_t *addr;		/* Address of allocation */
	size_t len;			/* Allocation length */
} stress_malloc_info_t;

#if defined(HAVE_LIB_PTHREAD)
/* per pthread data */
typedef struct {
        pthread_t pthread;      	/* The pthread */
        int       ret;          	/* pthread create return */
} stress_pthread_info_t;
#endif

typedef struct {
	stress_args_t *args;		/* args info */
	size_t instance;		/* per thread instance number */
	int rc;				/* return status */
} stress_malloc_args_t;

static const stress_help_t help[] = {
	{ NULL,	"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,	"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,	"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,	"malloc-mlock",		"attempt to mlock pages into memory" },
	{ NULL,	"malloc-ops N",		"stop after N malloc bogo operations" },
	{ NULL, "malloc-pthreads N",	"number of pthreads to run concurrently" },
	{ NULL,	"malloc-thresh N",	"threshold where malloc uses mmap instead of sbrk" },
	{ NULL, "malloc-touch",		"touch pages force pages to be populated" },
	{ NULL,	"malloc-zerofree",	"zero free'd memory" },
	{ NULL, "malloc-trim",		"enable malloc trimming" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_malloc_bytes,	"malloc-bytes",     TYPE_ID_SIZE_T_BYTES_VM, MIN_MALLOC_BYTES, MAX_MALLOC_BYTES, NULL },
	{ OPT_malloc_max,	"malloc-max",       TYPE_ID_SIZE_T_BYTES_VM, MIN_MALLOC_MAX, MAX_MALLOC_MAX, NULL },
	{ OPT_malloc_mlock,	"malloc-mlock",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_malloc_pthreads,	"malloc-pthreads",  TYPE_ID_SIZE_T, MIN_MALLOC_PTHREADS, MAX_MALLOC_PTHREADS, NULL },
	{ OPT_malloc_threshold,	"malloc-thresh",    TYPE_ID_SIZE_T_BYTES_VM, MIN_MALLOC_THRESHOLD, MAX_MALLOC_THRESHOLD, NULL },
	{ OPT_malloc_touch,	"malloc-touch",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_malloc_trim,	"malloc-trim",      TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_malloc_zerofree,	"malloc-zerofree",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)

static bool malloc_mlock;		/* True = mlock all future allocs */
static bool malloc_touch;		/* True = will touch allocate pages */
static bool malloc_trim_opt;		/* True = periodically trim malloc arena */
static size_t malloc_max;		/* Maximum number of allocations */
static size_t malloc_bytes;		/* Maximum per-allocation size */
static void *counter_lock;		/* Counter lock */
static const char *alloc_action = NULL;
static size_t alloc_size = 0;
static volatile bool do_jmp = true;	/* SIGSEGV jmp handler, longjmp back if true */
static sigjmp_buf jmp_env;		/* SIGSEGV jmp environment */
#if defined(HAVE_LIB_PTHREAD)
static volatile bool keep_thread_running_flag;	/* False to stop pthreads */
#endif

static void (*free_func)(void *ptr, size_t len);

static inline ALWAYS_INLINE void stress_alloc_action(const char *str, const size_t size)
{
	alloc_action = str;
	alloc_size = size;
}

/*
 *  stress_malloc_free()
 *	standard free, ignore length
 */
static void stress_malloc_free(void *ptr, size_t len)
{
	(void)len;

	free(ptr);
}

/*
 *  stress_malloc_zerofree()
 *	zero memory and free
 */
static void stress_malloc_zerofree(void *ptr, size_t len)
{
	if (LIKELY(len))
		(void)shim_memset(ptr, 0, len);
	free(ptr);
}

/*
 *  stress_alloc_size()
 *	get a new allocation size, ensuring
 *	it is never zero bytes.
 */
static inline size_t stress_alloc_size(const size_t size)
{
	const size_t len = stress_mwc64modn(size);
	const size_t min_size = sizeof(uintptr_t);

	return (len >= min_size) ? len : min_size;
}

static void stress_malloc_page_touch(
	uint8_t *buffer,
	const size_t size,
	const size_t page_size)
{
	stress_alloc_action("page_touch", size);
	if (malloc_touch) {
		register uint8_t *ptr;
		const uint8_t *end = buffer + size;

		for (ptr = buffer; LIKELY(stress_continue_flag() && (ptr < end)); ptr += page_size)
			*ptr = 0xff;
	} else {
		(void)stress_mincore_touch_pages_interruptible(buffer, size);
	}
}

static void *stress_malloc_loop(void *ptr)
{
	stress_malloc_args_t *malloc_args = (stress_malloc_args_t *)ptr;
	register stress_malloc_info_t *info;
	stress_args_t *args = malloc_args->args;
	const size_t page_size = args->page_size;
	const size_t info_size = malloc_max * sizeof(*info);
	size_t j;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#if defined(HAVE_MALLOC_TRIM)
	register uint16_t trim_counter = 0;
#endif

#if defined(MCL_FUTURE)
	if (malloc_mlock) {
		stress_alloc_action("mlockall", 0);
		(void)shim_mlockall(MCL_FUTURE);
	}
#endif
	stress_alloc_action("mmap", info_size);
	info = (stress_malloc_info_t *)stress_mmap_populate(NULL, info_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS,
			-1, 0);
	if (info == MAP_FAILED) {
		pr_inf("%s: failed to mmap address buffer of size %zu bytes%s, errno=%d (%s)\n",
			args->name, info_size, stress_get_memfree_str(),
			errno, strerror(errno));
		malloc_args->rc = EXIT_FAILURE;
		return &g_nowt;
	}
	stress_set_vma_anon_name(info, info_size, "malloc-info");
	for (;;) {
		const unsigned int rnd = stress_mwc32();
		const unsigned int i = rnd % malloc_max;
		const bool action = (rnd >> 12) & 1;
		const unsigned int do_calloc = (rnd >> 14) & 0x1f;
		const bool low_mem = ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(malloc_bytes / 2));

		shim_builtin_prefetch(&info[i]);

		/*
		 * With many instances running it is wise to
		 * double check before the next allocation as
		 * sometimes process start up is delayed for
		 * some time and we should bail out before
		 * exerting any more memory pressure
		 */
#if defined(HAVE_LIB_PTHREAD)
		if (UNLIKELY(!keep_thread_running_flag))
			break;
#endif
		if (info[i].addr) {
			/* 50% free, 50% realloc */
			if (action || low_mem) {
				if (UNLIKELY(verify && (uintptr_t)info[i].addr != *info[i].addr)) {
					pr_fail("%s: allocation at %p does not contain correct value\n",
						args->name, (void *)info[i].addr);
					malloc_args->rc = EXIT_FAILURE;
					break;
				}
				stress_alloc_action("free", info[i].len);
				free_func(info[i].addr, info[i].len);
				info[i].addr = NULL;
				info[i].len = 0;
				if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
					break;
			} else {
				void *tmp;
				const size_t len = stress_alloc_size(malloc_bytes);

				stress_alloc_action("realloc", len);
				tmp = realloc(info[i].addr, len);
				if (tmp) {
					info[i].addr = tmp;
					info[i].len = len;

					stress_malloc_page_touch((void *)info[i].addr, info[i].len, page_size);
					*info[i].addr = (uintptr_t)info[i].addr;	/* stash address */
					if (UNLIKELY(verify && (uintptr_t)info[i].addr != *info[i].addr)) {
						pr_fail("%s: allocation at %p does not contain correct value\n",
							args->name, (void *)info[i].addr);
						malloc_args->rc = EXIT_FAILURE;
						break;
					}
					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
						break;
				}
			}
		} else {
			if (action && !low_mem) {
				size_t n, len = stress_alloc_size(malloc_bytes);
#if defined(HAVE_ALIGNED_ALLOC) &&	\
    !defined(__OpenBSD__)
				size_t tmp_align;
#endif

				switch (do_calloc) {
				case 0:
					n = ((rnd >> 15) % 17) + 1;
					/* Avoid zero len / n being less than uintptr_t */
					if (len < (n * sizeof(uintptr_t)))
						len = n * sizeof(uintptr_t);
					stress_alloc_action("calloc", len);
					info[i].addr = (void *)calloc(n, len / n);
					len = n * (len / n);
					break;
#if defined(HAVE_POSIX_MEMALIGN)
				case 1:
					/* POSIX.1-2001 and POSIX.1-2008 */
					stress_alloc_action("posix_memalign", len);
					if (UNLIKELY(posix_memalign((void **)&info[i].addr, MK_ALIGN(i), len) != 0))
						info[i].addr = NULL;
					break;
#endif
#if defined(HAVE_ALIGNED_ALLOC) &&	\
    !defined(__OpenBSD__)
				case 2:
					/* C11 aligned allocation */
					tmp_align = MK_ALIGN(i);
					/* round len to multiple of alignment */
					len = (len + tmp_align - 1) & ~(tmp_align - 1);
					stress_alloc_action("aligned_alloc", len);
					info[i].addr = aligned_alloc(tmp_align, len);
					break;
#endif
#if defined(HAVE_MEMALIGN)
				case 3:
					/* SunOS 4.1.3 */
					stress_alloc_action("memalign", len);
					info[i].addr = memalign(MK_ALIGN(i), len);
					break;
#endif
#if defined(HAVE_VALLOC) &&	\
    !defined(HAVE_LIB_PTHREAD)
				case 4:
					stress_alloc_action("valloc", len);
					info[i].addr = valloc(len);
					break;
#elif defined(HAVE_MEMALIGN)
				case 4:
					stress_alloc_action("memalign", len);
					info[i].addr = memalign(page_size, len);
					break;
#endif
				default:
					stress_alloc_action("malloc", len);
					info[i].addr = (uintptr_t *)malloc(len);
					break;
				}
				if (LIKELY(info[i].addr != NULL)) {
					stress_alloc_action("malloc", len);
					stress_malloc_page_touch((void *)info[i].addr, len, page_size);
					*info[i].addr = (uintptr_t)info[i].addr;	/* stash address */
					info[i].len = len;

					if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
						stress_cpu_data_cache_flush((void *)info[i].addr, len);

					if (UNLIKELY(!stress_bogo_inc_lock(args, counter_lock, true)))
						break;

#if defined(HAVE_MALLOC_USABLE_SIZE)
					/* add some sanity checking */
					if (UNLIKELY(verify)) {
						const size_t usable_size = malloc_usable_size(info[i].addr);

						if (UNLIKELY(usable_size < len)) {
							pr_fail("%s: malloc_usable_size on %p returned a "
								"value %zu, expected %zu or larger\n",
								args->name, info[i].addr, usable_size, len);
							malloc_args->rc = EXIT_FAILURE;
							break;
						}
					}
#endif
				} else {
					info[i].len = 0;
				}
			}
		}
#if defined(HAVE_MALLOC_TRIM)
		if (malloc_trim_opt && (trim_counter++ == 0)) {
			stress_alloc_action("malloc_trim", 0);
			(void)malloc_trim(0);
		}
#endif
	}

	for (j = 0; j < malloc_max; j++) {
		if (verify && info[j].addr && ((uintptr_t)info[j].addr != *info[j].addr)) {
			pr_fail("%s: allocation at %p does not contain correct value\n",
				args->name, (void *)info[j].addr);
			malloc_args->rc = EXIT_FAILURE;
		}
		stress_alloc_action("free", info[j].len);
		free_func(info[j].addr, info[j].len);
	}
	stress_alloc_action("munmap", info_size);
	(void)munmap((void *)info, info_size);

	return &g_nowt;
}

static void MLOCKED_TEXT stress_malloc_sigsegv_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
		stress_no_return();
	}
}

static int stress_malloc_child(stress_args_t *args, void *context)
{
	int ret;
	NOCLOBBER int rc = EXIT_SUCCESS;
	/*
	 *  pthread instance 0 is actually the main child process,
	 *  instances 1..N are pthreads 0..N-1
	 */
	stress_malloc_args_t malloc_args[MAX_MALLOC_PTHREADS + 1];
	size_t malloc_pthreads = DEFAULT_MALLOC_PTHREADS;
#if defined(HAVE_LIB_PTHREAD)
	stress_pthread_info_t pthreads[MAX_MALLOC_PTHREADS];
	size_t j;
#endif

	(void)shim_memset(malloc_args, 0, sizeof(malloc_args));

	ret = sigsetjmp(jmp_env, 1);
	if (ret == 1) {
		do_jmp = false;
		pr_fail("%s: unexpected SIGSEGV occurred after allocating %zu bytes using %s(), exiting immediately\n", args->name, alloc_size, alloc_action);
		return EXIT_FAILURE;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_malloc_sigsegv_handler, NULL) < 0)
		return EXIT_FAILURE;

	if (!stress_get_setting("malloc-pthreads", &malloc_pthreads)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			malloc_pthreads = MAX_MALLOC_PTHREADS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			malloc_pthreads = MIN_MALLOC_PTHREADS;
	}

#if defined(MCL_FUTURE)
	if (malloc_mlock) {
		stress_alloc_action("mlockall", 0);
		(void)shim_mlockall(MCL_FUTURE);
	}
#endif

	malloc_args[0].args = args;
	malloc_args[0].instance = 0;
	malloc_args[0].rc = EXIT_SUCCESS;

	(void)context;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_LIB_PTHREAD)
	keep_thread_running_flag = true;
	(void)shim_memset(pthreads, 0, sizeof(pthreads));
	for (j = 0; j < malloc_pthreads; j++) {
		malloc_args[j + 1].args = args;
		malloc_args[j + 1].instance = j + 1;
		malloc_args[j + 1].rc = EXIT_SUCCESS;
		pthreads[j].ret = pthread_create(&pthreads[j].pthread, NULL,
			stress_malloc_loop, (void *)&malloc_args[j + 1]);
	}
#else
	if (stress_instance_zero(args) && (malloc_pthreads > 0))
		pr_inf("%s: pthreads not supported, ignoring the "
			"--malloc-pthreads option\n", args->name);
#endif
	stress_malloc_loop(&malloc_args[0]);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_LIB_PTHREAD)
	keep_thread_running_flag = false;

	if (malloc_args[0].rc == EXIT_FAILURE)
		rc = EXIT_FAILURE;

	for (j = 0; j < malloc_pthreads; j++) {
		if (pthreads[j].ret)
			continue;

		ret = pthread_join(pthreads[j].pthread, NULL);
		if ((ret) && (ret != ESRCH)) {
			pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
				args->name, ret, strerror(ret));
		}
		if (malloc_args[j].rc == EXIT_FAILURE)
			rc = EXIT_FAILURE;
	}
#endif
	return rc;
}

/*
 *  stress_malloc()
 *	stress malloc by performing a mix of
 *	allocation and frees
 */
static int stress_malloc(stress_args_t *args)
{
	int ret;
	bool malloc_zerofree = false;

	stress_alloc_action("<unknown>", 0);

	counter_lock = stress_lock_create("counter");
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	malloc_bytes = DEFAULT_MALLOC_BYTES;
	if (!stress_get_setting("malloc-bytes", &malloc_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			malloc_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			malloc_bytes = MIN_MALLOC_BYTES;
	}
	malloc_bytes /= args->instances;
	if (malloc_bytes < MIN_MALLOC_BYTES)
		malloc_bytes = MIN_MALLOC_BYTES;

	malloc_max = DEFAULT_MALLOC_MAX;
	if (!stress_get_setting("malloc-max", &malloc_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			malloc_max = MAX_MALLOC_MAX;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			malloc_max = MIN_MALLOC_MAX;
	}

#if defined(HAVE_COMPILER_GCC_OR_MUSL) && 	\
    defined(HAVE_MALLOPT) &&			\
    defined(M_MMAP_THRESHOLD)
	{
		size_t malloc_threshold = DEFAULT_MALLOC_THRESHOLD;

		if (stress_get_setting("malloc-threshold", &malloc_threshold))
			(void)mallopt(M_MMAP_THRESHOLD, (int)malloc_threshold);
	}
#endif

	malloc_touch = false;
	(void)stress_get_setting("malloc-touch", &malloc_touch);
	malloc_trim_opt = false;
	(void)stress_get_setting("malloc-trim", &malloc_trim_opt);
	malloc_mlock = false;
	(void)stress_get_setting("malloc-mlock", &malloc_mlock);
	(void)stress_get_setting("malloc-zerofree", &malloc_zerofree);
	free_func = malloc_zerofree ? stress_malloc_zerofree : stress_malloc_free;

	ret = stress_oomable_child(args, NULL, stress_malloc_child, STRESS_OOMABLE_NORMAL);

	(void)stress_lock_destroy(counter_lock);

	return ret;
}

const stressor_info_t stress_malloc_info = {
	.stressor = stress_malloc,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

const stressor_info_t stress_malloc_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
