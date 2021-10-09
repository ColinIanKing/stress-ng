/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#define MAX_MALLOC_PTHREADS		(32)

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

#if defined(HAVE_LIB_PTHREAD)
/* per pthread data */
typedef struct {
        pthread_t pthread;      /* The pthread */
        int       ret;          /* pthread create return */
} stress_pthread_info_t;
#endif

typedef struct {
	const stress_args_t *args;			/* args info */
	uint64_t *counters;				/* bogo op counters */
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

	return len ? len : 1;
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

/*
 *  stress_malloc_racy_count()
 *	racy bogo op counter, we have a lot of contention
 *	if we lock the args->counter, so sum per-process
 *	counters in a racy way.
 */
static uint64_t stress_malloc_racy_count(const uint64_t *counters)
{
	register uint64_t count = 0;
	register size_t i;

	for (i = 0; i < malloc_pthreads + 1; i++)
		count += counters[i];

	return count;
}

/*
 *  stress_malloc_keep_stressing(args)
 *      check if SIGALRM has triggered to the bogo ops count
 *      has been reached, counter is racy, but that's OK
 */
static bool HOT OPTIMIZE3 stress_malloc_keep_stressing(
        const stress_args_t *args,
        uint64_t *counters)
{
        return (LIKELY(g_keep_stressing_flag) &&
                LIKELY(!args->max_ops ||
                (stress_malloc_racy_count(counters) < args->max_ops)));
}

static void *stress_malloc_loop(void *ptr)
{
	const stress_malloc_args_t *malloc_args = (stress_malloc_args_t *)ptr;
	const stress_args_t *args = malloc_args->args;
	const size_t page_size = args->page_size;
	uint64_t *counters = malloc_args->counters;
	uint64_t *counter = &counters[malloc_args->instance];
	void **addr;
	static void *nowt = NULL;
	size_t j;

	addr = (void **)calloc(malloc_max, sizeof(*addr));
	if (!addr) {
		pr_dbg("%s: cannot allocate address buffer: %d (%s)\n",
			args->name, errno, strerror(errno));
		return &nowt;
	}

	for (;;) {
		const unsigned int rnd = stress_mwc32();
		const unsigned int i = rnd % malloc_max;
		const unsigned int action = (rnd >> 12) & 1;
		const unsigned int do_calloc = (rnd >> 14) & 0x1f;

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
		if (!stress_malloc_keep_stressing(args, counters))
			break;

		if (addr[i]) {
			/* 50% free, 50% realloc */
			if (action) {
				free(addr[i]);
				addr[i] = NULL;
				(*counter)++;
			} else {
				void *tmp;
				const size_t len = stress_alloc_size(malloc_bytes);

				tmp = realloc(addr[i], len);
				if (tmp) {
					addr[i] = tmp;
					stress_malloc_page_touch(addr[i], len, page_size);
					(*counter)++;
				}
			}
		} else {
			/* 50% free, 50% alloc */
			if (action) {
				size_t len = stress_alloc_size(malloc_bytes);

				if (do_calloc == 0) {
					size_t n = ((rnd >> 15) % 17) + 1;
					addr[i] = calloc(n, len / n);
					len = n * (len / n);
				} else {
					addr[i] = malloc(len);
				}
				if (addr[i]) {
					stress_malloc_page_touch(addr[i], len, page_size);
					(*counter)++;
				}
			}
		}
	}

	for (j = 0; j < malloc_max; j++) {
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
	uint64_t counters[MAX_MALLOC_PTHREADS + 1];
	stress_malloc_args_t malloc_args[MAX_MALLOC_PTHREADS + 1];

	(void)memset(counters, 0, sizeof(counters));
	(void)memset(malloc_args, 0, sizeof(malloc_args));

	malloc_args[0].args = args;
	malloc_args[0].counters = counters;
	malloc_args[0].instance = 0;

	(void)context;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_LIB_PTHREAD)
	keep_thread_running_flag = true;
	(void)memset(pthreads, 0, sizeof(pthreads));
	for (j = 0; j < malloc_pthreads; j++) {
		malloc_args[j + 1].args = args;
		malloc_args[j + 1].counters = counters;
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

	set_counter(args, stress_malloc_racy_count(counters));

	return EXIT_SUCCESS;
}

/*
 *  stress_malloc()
 *	stress malloc by performing a mix of
 *	allocation and frees
 */
static int stress_malloc(const stress_args_t *args)
{
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

	return stress_oomable_child(args, NULL, stress_malloc_child, STRESS_OOMABLE_NORMAL);
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
	.help = help
};
