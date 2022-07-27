/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#define MAX_MALLOC_PTHREADS	(32)

#define MK_ALIGN(x)	(1U << (3 + ((x) & 7)))

static size_t malloc_max;		/* Maximum number of allocations */
static size_t malloc_bytes;		/* Maximum per-allocation size */
#if defined(HAVE_LIB_PTHREAD)
static volatile bool keep_thread_running_flag;	/* False to stop pthreads */
#endif
static size_t malloc_pthreads;		/* Number of pthreads */
#if defined(__GNUC__) &&	\
    defined(HAVE_MALLOPT) &&	\
    defined(M_MMAP_THRESHOLD)
static size_t malloc_threshold;		/* When to use mmap and not sbrk */
#endif
static bool malloc_touch;		/* True will touch allocate pages */
static void *counter_lock;		/* Counter lock */

#if defined(HAVE_LIB_PTHREAD)
/* per pthread data */
typedef struct {
        pthread_t pthread;      /* The pthread */
        int       ret;          /* pthread create return */
} stress_pthread_info_t;
#endif

typedef struct {
	const stress_args_t *args;			/* args info */
	size_t instance;				/* per thread instance number */
} stress_malloc_args_t;

static const stress_help_t help[] = {
	{ NULL,	"malloc N",		"start N workers exercising malloc/realloc/free" },
	{ NULL,	"malloc-bytes N",	"allocate up to N bytes per allocation" },
	{ NULL,	"malloc-max N",		"keep up to N allocations at a time" },
	{ NULL,	"malloc-ops N",		"stop after N malloc bogo operations" },
	{ NULL,	"malloc-thresh N",	"threshold where malloc uses mmap instead of sbrk" },
	{ NULL, "malloc-pthreads N",	"number of pthreads to run concurrently" },
	{ NULL, "malloc-touch",		"touch pages force pages to be populated" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_malloc_bytes(const char *opt)
{
	size_t bytes;

	bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("malloc-bytes", bytes,
		MIN_MALLOC_BYTES, MAX_MEM_LIMIT);
	return stress_set_setting("malloc-bytes", TYPE_ID_SIZE_T, &bytes);
}

static int stress_set_malloc_max(const char *opt)
{
	size_t max;

	max = (size_t)stress_get_uint64_byte(opt);
	stress_check_range("malloc-max", max,
		MIN_MALLOC_MAX, MAX_MALLOC_MAX);
	return stress_set_setting("malloc-max", TYPE_ID_SIZE_T, &max);
}

static int stress_set_malloc_threshold(const char *opt)
{
	size_t threshold;

	threshold = (size_t)stress_get_uint64_byte(opt);
	stress_check_range("malloc-threshold", threshold,
		MIN_MALLOC_THRESHOLD, MAX_MALLOC_THRESHOLD);
	return stress_set_setting("malloc-threshold", TYPE_ID_SIZE_T, &threshold);
}

static int stress_set_malloc_pthreads(const char *opt)
{
	size_t npthreads;

	npthreads = (size_t)stress_get_uint64_byte(opt);
	stress_check_range("malloc-pthreads", npthreads,
		0, MAX_MALLOC_PTHREADS);
	return stress_set_setting("malloc-pthreads", TYPE_ID_SIZE_T, &npthreads);
}

static int stress_set_malloc_touch(const char *opt)
{
	bool malloc_touch_tmp = true;

	(void)opt;
	return stress_set_setting("malloc-touch", TYPE_ID_BOOL, &malloc_touch_tmp);
}

/*
 *  stress_alloc_size()
 *	get a new allocation size, ensuring
 *	it is never zero bytes.
 */
static inline size_t stress_alloc_size(const size_t size)
{
	const size_t len = stress_mwc64() % size;
	const size_t min_size = sizeof(uintptr_t);

	return len >= min_size ? len : min_size;
}

static void stress_malloc_page_touch(
	uint8_t *buffer,
	const size_t size,
	const size_t page_size)
{

	if (malloc_touch) {
		register uint8_t *ptr;
		const uint8_t *end = buffer + size;

		for (ptr = buffer; keep_stressing_flag() && (ptr < end); ptr += page_size)
			*ptr = 0xff;
	} else {
		(void)stress_mincore_touch_pages_interruptible(buffer, size);
	}
}

static void *stress_malloc_loop(void *ptr)
{
	const stress_malloc_args_t *malloc_args = (stress_malloc_args_t *)ptr;
	const stress_args_t *args = malloc_args->args;
	const size_t page_size = args->page_size;
	uintptr_t **addr;
	static void *nowt = NULL;
	size_t j;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	addr = (uintptr_t **)calloc(malloc_max, sizeof(*addr));
	if (!addr) {
		pr_dbg("%s: cannot allocate address buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		return &nowt;
	}

	for (;;) {
		const unsigned int rnd = stress_mwc32();
		const unsigned int i = rnd % malloc_max;
		const unsigned int action = (rnd >> 12);
		const unsigned int do_calloc = (rnd >> 14) & 0x1f;
#if defined(HAVE_MALLOC_TRIM)
		const unsigned int do_trim = (rnd & 0x7);
#endif

		/*
		 * With many instances running it is wise to
		 * double check before the next allocation as
		 * sometimes process start up is delayed for
		 * some time and we should bail out before
		 * exerting any more memory pressure
		 */
#if defined(HAVE_LIB_PTHREAD)
		if (!keep_thread_running_flag)
			break;
#endif
		if (!inc_counter_lock(args, counter_lock, false))
			break;

		if (addr[i]) {
			/* 50% free, 50% realloc */
			if (action) {
				if (verify && (uintptr_t)addr[i] != *addr[i]) {
					pr_fail("%s: allocation at %p does not contain correct value\n",
						args->name, (void *)addr[i]);
				}
				free(addr[i]);
				addr[i] = NULL;

				if (!inc_counter_lock(args, counter_lock, true))
					break;
			} else {
				void *tmp;
				const size_t len = stress_alloc_size(malloc_bytes);

				tmp = realloc(addr[i], len);
				if (tmp) {
					addr[i] = tmp;
					stress_malloc_page_touch((void *)addr[i], len, page_size);
					*addr[i] = (uintptr_t)addr[i];	/* stash address */
					if (verify && (uintptr_t)addr[i] != *addr[i]) {
						pr_fail("%s: allocation at %p does not contain correct value\n",
							args->name, (void *)addr[i]);
					}
					if (!inc_counter_lock(args, counter_lock, true))
						break;
				}
			}
		} else {
			/* 50% free, 50% alloc */
			if (action) {
				size_t len = stress_alloc_size(malloc_bytes);

				switch (do_calloc) {
				case 0:
					size_t n = ((rnd >> 15) % 17) + 1;
					addr[i] = calloc(n, len / n);
					len = n * (len / n);
					break;
#if defined(HAVE_POSIX_MEMALIGN)
				case 1:
					/* POSIX.1-2001 and POSIX.1-2008 */
					if (posix_memalign((void **)&addr[i], MK_ALIGN(i), len) != 0)
						addr[i] = NULL;
					break;
#endif
#if defined(HAVE_ALIGNED_ALLOC)
				case 2:
					/* C11 aligned allocation */
					addr[i] = aligned_alloc(MK_ALIGN(i), len);
					break;
#endif
#if defined(HAVE_MEMALIGN)
				case 3:
					/* SunOS 4.1.3 */
					addr[i] = memalign(MK_ALIGN(i), len);
					break;
#endif
				default:
					addr[i] = malloc(len);
					break;
				}
				if (addr[i]) {
					stress_malloc_page_touch((void *)addr[i], len, page_size);
					*addr[i] = (uintptr_t)addr[i];	/* stash address */
					if (!inc_counter_lock(args, counter_lock, true))
						break;
				}
			}
		}
#if defined(HAVE_MALLOC_TRIM)
		if (do_trim == 0)
			(void)malloc_trim(0);
#endif
	}

	for (j = 0; j < malloc_max; j++) {
		if (verify && addr[j] && (uintptr_t)addr[j] != *addr[j]) {
			pr_fail("%s: allocation at %p does not contain correct value\n",
				args->name, (void *)addr[j]);
		}
		free(addr[j]);
	}
	free(addr);

	return &nowt;
}

static int stress_malloc_child(const stress_args_t *args, void *context)
{
#if defined(HAVE_LIB_PTHREAD)
	stress_pthread_info_t pthreads[MAX_MALLOC_PTHREADS];
	size_t j;
#endif
	/*
	 *  pthread instance 0 is actually the main child process,
	 *  insances 1..N are pthreads 0..N-1
	 */
	stress_malloc_args_t malloc_args[MAX_MALLOC_PTHREADS + 1];

	(void)memset(malloc_args, 0, sizeof(malloc_args));

	malloc_args[0].args = args;
	malloc_args[0].instance = 0;

	(void)context;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_LIB_PTHREAD)
	keep_thread_running_flag = true;
	(void)memset(pthreads, 0, sizeof(pthreads));
	for (j = 0; j < malloc_pthreads; j++) {
		malloc_args[j + 1].args = args;
		malloc_args[j + 1].instance = j + 1;
		pthreads[j].ret = pthread_create(&pthreads[j].pthread, NULL,
			stress_malloc_loop, (void *)&malloc_args);
	}
#else
	if ((args->instance == 0) && (malloc_pthreads > 0))
		pr_inf("%s: pthreads not supported, ignoring the "
			"--malloc-pthreads option\n", args->name);
#endif
	stress_malloc_loop(&malloc_args);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_LIB_PTHREAD)
	keep_thread_running_flag = false;
	for (j = 0; j < malloc_pthreads; j++) {
		int ret;

		if (pthreads[j].ret)
			continue;

		ret = pthread_join(pthreads[j].pthread, NULL);
		if ((ret) && (ret != ESRCH)) {
			pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
				args->name, ret, strerror(ret));
		}
	}
#endif

	return EXIT_SUCCESS;
}

/*
 *  stress_malloc()
 *	stress malloc by performing a mix of
 *	allocation and frees
 */
static int stress_malloc(const stress_args_t *args)
{
	int ret;

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	malloc_bytes = DEFAULT_MALLOC_BYTES;
	if (!stress_get_setting("malloc-bytes", &malloc_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			malloc_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			malloc_bytes = MIN_MALLOC_BYTES;
	}
	malloc_bytes /= args->num_instances;
	if (malloc_bytes < MIN_MALLOC_BYTES)
		malloc_bytes = MIN_MALLOC_BYTES;

	malloc_max = DEFAULT_MALLOC_MAX;
	if (!stress_get_setting("malloc-max", &malloc_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			malloc_max = MAX_MALLOC_MAX;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			malloc_max = MIN_MALLOC_MAX;
	}

#if defined(__GNUC__) && 	\
    defined(HAVE_MALLOPT) &&	\
    defined(M_MMAP_THRESHOLD)
	malloc_threshold = DEFAULT_MALLOC_THRESHOLD;
	if (stress_get_setting("malloc-threshold", &malloc_threshold))
		(void)mallopt(M_MMAP_THRESHOLD, (int)malloc_threshold);
#endif
	malloc_pthreads = 0;
	(void)stress_get_setting("malloc-pthreads", &malloc_pthreads);

	malloc_touch = false;
	(void)stress_get_setting("malloc-touch", &malloc_touch);

	ret = stress_oomable_child(args, NULL, stress_malloc_child, STRESS_OOMABLE_NORMAL);

	(void)stress_lock_destroy(counter_lock);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_malloc_max,	stress_set_malloc_max },
	{ OPT_malloc_bytes,	stress_set_malloc_bytes },
	{ OPT_malloc_pthreads,	stress_set_malloc_pthreads },
	{ OPT_malloc_threshold,	stress_set_malloc_threshold },
	{ OPT_malloc_touch,	stress_set_malloc_touch },
	{ 0,		NULL }
};

stressor_info_t stress_malloc_info = {
	.stressor = stress_malloc,
	.class = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
