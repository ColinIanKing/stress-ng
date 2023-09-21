// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-out-of-memory.h"

typedef struct {
	const stress_args_t *args;	/* stress-ng arguments */
	size_t page_shift;		/* log2(page_size) */
	char *exec_path;		/* path of executable */
	double duration;		/* mmap run time duration */
	double count;			/* count of mmap calls */
} munmap_context_t;

static const stress_help_t help[] = {
	{ NULL,	"munmap N",	 "start N workers stressing munmap" },
	{ NULL,	"munmap-ops N",	 "stop after N munmap bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(__linux__)
/*
 *  stress_munmap_log2()
 *	slow but simple log to the base 2 of n
 */
static inline size_t stress_munmap_log2(size_t n)
{
#if defined(HAVE_BUILTIN_CLZL)
	return (8 * sizeof(n)) - __builtin_clzl(n) - 1;
#else
	register size_t l2;

	for (l2 = 0; n > 1; l2++)
		n >>= 1;

	return l2;
#endif
}

/*
 *  stress_munmap_stride()
 *	find a prime that is greater than n and not
 *	a multiple of n for a page unmapping stride
 */
static size_t stress_munmap_stride(const size_t n)
{
	register size_t p;

	for (p = n + 1; ; p++) {
		if ((n % p) && stress_is_prime64((uint64_t)p))
			return p;
	}
}

/*
 *  stress_munmap_range()
 *	unmap a mmap'd region using a prime sized stride across the
 *	mmap'd region to create lots of temporary mapping holes.
 */
static void stress_munmap_range(
	const stress_args_t *args,
	void *start,
	void *end,
	munmap_context_t *ctxt)
{
	const size_t page_shift = ctxt->page_shift;
	const size_t page_size = args->page_size;
	const size_t size = (uintptr_t)end - (uintptr_t)start;
	const size_t n_pages = size / page_size;
	const size_t stride = stress_munmap_stride(n_pages + stress_mwc8());
	size_t i, j;

	for (i = 0, j = 0; stress_continue(args) && (i < n_pages); i++) {
		const size_t offset = j << page_shift;
		void *addr = ((uint8_t *)start) + offset;
		double t;

		t = stress_time_now();
		if (stress_munmap_retry_enomem(addr, page_size) == 0) {
			unsigned char vec[1];

			ctxt->duration += stress_time_now() - t;
			ctxt->count += 1.0;
			stress_bogo_inc(args);

			if ((shim_mincore(addr, page_size, vec) == 0) &&
			    (vec[0] != 0)) {
				pr_fail("%s: unmapped page %p still resident in memory\n",
					args->name, addr);
			}
		}
		j += stride;
		j %= n_pages;
	}
}

/*
 *  stress_munmap_sig_handler()
 *	signal handler to immediately terminates
 */
static void NORETURN MLOCKED_TEXT stress_munmap_sig_handler(int num)
{
	(void)num;

	_exit(0);
}

/*
 *  stress_munmap_child()
 *	child process that attempts to unmap a lot of the
 *	pages mapped into stress-ng without killing itself with
 *	a bus error or segmentation fault.
 */
static int stress_munmap_child(const stress_args_t *args, void *context)
{
	FILE *fp;
	char path[PATH_MAX];
	char buf[4096], prot[5];
	const pid_t pid = getpid();
	munmap_context_t *ctxt = (munmap_context_t *)context;
	void *start, *end, *offset;
	int major, minor, n;
	uint64_t inode;

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV, stress_munmap_sig_handler, NULL));
	VOID_RET(int, stress_sighandler(args->name, SIGBUS, stress_munmap_sig_handler, NULL));

	(void)snprintf(path, sizeof(path), "/proc/%" PRIdMAX "/maps", (intmax_t)pid);
	fp = fopen(path, "r");
	if (!fp)
		return EXIT_NO_RESOURCE;
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_DONTDUMP)
	/*
	 *  Vainly attempt to reduce any potential core dump size
	 */
	while (stress_continue(args) && fgets(buf, sizeof(buf), fp)) {
		size_t size;

		*path = '\0';
		n = sscanf(buf, "%p-%p %4s %p %x:%x %" PRIu64 " %s\n",
			&start, &end, prot, &offset, &major, &minor,
			&inode, path);
		if (n < 7)
			continue;	/* bad sscanf data */
		if (start >= end)
			continue;	/* invalid address range */
		size = (uintptr_t)end - (uintptr_t)start;
		(void)madvise(start, size, MADV_DONTDUMP);
	}
	(void)rewind(fp);
#endif
	while (stress_continue(args) && fgets(buf, sizeof(buf), fp)) {
		*path = '\0';
		n = sscanf(buf, "%p-%p %4s %p %x:%x %" PRIu64 " %s\n",
			&start, &end, prot, &offset, &major, &minor,
			&inode, path);
		/*
		 *  Filter out mappings that we should avoid from
		 *  unmapping
		 */
		if (n < 7)
			continue;	/* bad sscanf data */
		if (start >= end)
			continue;	/* invalid address range */
		if (start == context)
			continue;	/* don't want to unmap shared context */
		if (((const void *)args >= start) && ((const void *)args < end))
			continue;	/* don't want to unmap shard args */
		if (!path[0])
			continue;	/* don't unmap anonymous mappings */
		if (path[0] == '[')
			continue;	/* don't unmap special mappings (stack, vdso etc) */
		if (strstr(path, "libc"))
			continue;	/* don't unmap libc */
		if (strstr(path, "/dev/zero"))
			continue;	/* need this for zero'd page data */
		if (!strcmp(path, ctxt->exec_path))
			continue;	/* don't unmap stress-ng */
		if (prot[0] != 'r')
			continue;	/* don't unmap non-readable pages */
		if (prot[2] == 'x')
			continue;	/* don't unmap executable pages */
		stress_munmap_range(args, start, end, ctxt);
	}
	(void)fclose(fp);

	if (stress_continue(args))
		stress_bogo_inc(args);	/* bump per stressor */

	return EXIT_SUCCESS;
}

static inline void stress_munmap_clean_path(char *path)
{
	char *ptr = path;

	while (*ptr) {
		if (isspace((int)*ptr)) {
			*ptr = '\0';
			break;
		}
		ptr++;
	}
}

/*
 *  stress_munmap()
 *	stress munmap
 */
static int stress_munmap(const stress_args_t *args)
{
	munmap_context_t *ctxt;
	double rate;
	char exec_path[PATH_MAX];

	ctxt = mmap(NULL, sizeof(*ctxt), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ctxt == MAP_FAILED) {
		pr_inf_skip("%s: skipping stressor, cannot mmap context buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	ctxt->duration = 0.0;
	ctxt->count = 0.0;
	ctxt->args = args;
	ctxt->page_shift = stress_munmap_log2(args->page_size);
	ctxt->exec_path = stress_get_proc_self_exe(exec_path, sizeof(exec_path));
	if (!ctxt->exec_path) {
		pr_inf_skip("%s: skipping stressor, cannot determine child executable path\n",
			args->name);
		(void)munmap((void *)ctxt, sizeof(*ctxt));
		return EXIT_NO_RESOURCE;
	}
	stress_munmap_clean_path(ctxt->exec_path);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	while (stress_continue(args)) {
		VOID_RET(int, stress_oomable_child(args, (void *)ctxt, stress_munmap_child, STRESS_OOMABLE_QUIET));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = ctxt->count > 0.0 ? ctxt->duration / ctxt->count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per page mmap()", rate * STRESS_DBL_NANOSECOND);

	(void)munmap((void *)ctxt, sizeof(*ctxt));

	return EXIT_SUCCESS;
}

stressor_info_t stress_munmap_info = {
	.stressor = stress_munmap,
	.class = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else
stressor_info_t stress_munmap_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
        .help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
