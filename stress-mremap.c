// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-out-of-memory.h"

#define DEFAULT_MREMAP_BYTES	(256 * MB)
#define MIN_MREMAP_BYTES	(4 * KB)
#define MAX_MREMAP_BYTES	(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"mremap N",	  "start N workers stressing mremap" },
	{ NULL,	"mremap-bytes N", "mremap N bytes maximum for each stress iteration" },
	{ NULL, "mremap-mlock",	  "mlock remap pages, force pages to be unswappable" },
	{ NULL,	"mremap-ops N",	  "stop after N mremap bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_mremap_bytes(const char *opt)
{
	size_t mremap_bytes;

	mremap_bytes = (size_t)stress_get_uint64_byte_memory(opt, 1);
	stress_check_range_bytes("mremap-bytes", mremap_bytes,
		MIN_MREMAP_BYTES, MAX_MREMAP_BYTES);
	return stress_set_setting("mremap-bytes", TYPE_ID_SIZE_T, &mremap_bytes);
}

static int stress_set_mremap_mlock(const char *opt)
{
	return stress_set_setting_true("mremap-mlock", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mremap_bytes,	stress_set_mremap_bytes },
	{ OPT_mremap_mlock,	stress_set_mremap_mlock },
	{ 0,			NULL }
};

#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2,4,0)

#if defined(MREMAP_FIXED)
/*
 *  rand_mremap_addr()
 *	try and find a random unmapped region of memory
 */
static inline void *rand_mremap_addr(const size_t sz, int flags)
{
	void *addr;
	int mask = MREMAP_FIXED | MAP_SHARED;

#if defined(MAP_POPULATE)
	mask |= MAP_POPULATE;
#endif
	flags &= ~(mask);
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	(void)stress_munmap_retry_enomem(addr, sz);

	/*
	 * At this point, we know that we can remap to this addr
	 * in this process if we don't do any memory mappings between
	 * the munmap above and the remapping
	 */
	return addr;
}
#endif

/*
 *  try_remap()
 *	try and remap old size to new size
 */
static int try_remap(
	const stress_args_t *args,
	uint8_t **buf,
	const size_t old_sz,
	const size_t new_sz,
	const bool mremap_mlock,
	double *duration,
	double *count)
{
	uint8_t *newbuf;
	int retry, flags = 0;
	static int metrics_counter = 0;
#if defined(MREMAP_MAYMOVE)
	const int maymove = MREMAP_MAYMOVE;
#else
	const int maymove = 0;
#endif

#if defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)
	flags = maymove | (stress_mwc32() & MREMAP_FIXED);
#else
	flags = maymove;
#endif

	for (retry = 0; retry < 100; retry++) {
		double t = 0.0;


#if defined(MREMAP_FIXED)
		void *addr = rand_mremap_addr(new_sz + args->page_size, flags);
#endif
		if (!stress_continue_flag()) {
			(void)stress_munmap_retry_enomem(*buf, old_sz);
			*buf = 0;
			return 0;
		}
		if (UNLIKELY(metrics_counter == 0))
			t = stress_time_now();
#if defined(MREMAP_FIXED)
		if (addr) {
			newbuf = mremap(*buf, old_sz, new_sz, flags, addr);
		} else {
			newbuf = mremap(*buf, old_sz, new_sz, flags & ~MREMAP_FIXED);
		}
#else
		newbuf = mremap(*buf, old_sz, new_sz, flags);
#endif
		if (newbuf && (newbuf != MAP_FAILED)) {
			if (UNLIKELY(metrics_counter == 0)) {
				(*duration) += stress_time_now() - t;
				(*count) += 1.0;
			}
			*buf = newbuf;

#if defined(MREMAP_DONTUNMAP)
			/*
			 *  Move and explicitly don't unmap old mapping,
			 *  followed by an unmap of the old mapping for
			 *  some more exercise
			 */
			if (UNLIKELY(metrics_counter == 0))
				t = stress_time_now();
			newbuf = mremap(*buf, new_sz, new_sz,
					MREMAP_DONTUNMAP | MREMAP_MAYMOVE);
			if (newbuf && (newbuf != MAP_FAILED)) {
				if (UNLIKELY(metrics_counter == 0)) {
					(*duration) += stress_time_now() - t;
					(*count) += 1.0;
				}

				if (*buf)
					(void)stress_munmap_retry_enomem(*buf, new_sz);
				*buf = newbuf;
			}
#endif

#if defined(HAVE_MLOCK)
			if (mremap_mlock && *buf)
				(void)shim_mlock(*buf, new_sz);
#else
			(void)mremap_mlock;
#endif
			if (metrics_counter++ > 500)
				metrics_counter = 0;

			return 0;
		}

		switch (errno) {
		case ENOMEM:
		case EAGAIN:
			continue;
		case EINVAL:
#if defined(MREMAP_FIXED)
			/*
			 * Earlier kernels may not support this or we
			 * chose a bad random address, so just fall
			 * back to non fixed remapping
			 */
			if (flags & MREMAP_FIXED)
				flags &= ~MREMAP_FIXED;
#endif
			break;
		case EFAULT:
		default:
			break;
		}
	}
	pr_fail("%s: mremap failed, errno=%d (%s)\n",
		args->name, errno, strerror(errno));
	return -1;
}

static int stress_mremap_child(const stress_args_t *args, void *context)
{
	size_t new_sz, sz, mremap_bytes = DEFAULT_MREMAP_BYTES;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const size_t page_size = args->page_size;
	bool mremap_mlock = false;
	double duration = 0.0, count = 0.0, rate;
	int ret = EXIT_SUCCESS;

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	(void)context;

	if (!stress_get_setting("mremap-bytes", &mremap_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mremap_bytes = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mremap_bytes = MIN_MREMAP_BYTES;
	}
	mremap_bytes /= args->num_instances;
	if (mremap_bytes < MIN_MREMAP_BYTES)
		mremap_bytes = MIN_MREMAP_BYTES;
	if (mremap_bytes < page_size)
		mremap_bytes = page_size;
	new_sz = sz = mremap_bytes & ~(page_size - 1);

	(void)stress_get_setting("mremap-mlock", &mremap_mlock);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint8_t *buf = NULL, *ptr;
		size_t old_sz;

		if (!stress_continue_flag())
			goto deinit;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		(void)stress_madvise_random(buf, new_sz);
		(void)stress_mincore_touch_pages(buf, mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			stress_mmap_set(buf, new_sz, page_size);
			if (stress_mmap_check(buf, sz, page_size) < 0) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data\n",
					args->name, sz);
				(void)stress_munmap_retry_enomem(buf, new_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
		}

		old_sz = new_sz;
		new_sz >>= 1;
		while (new_sz > page_size) {
			if (try_remap(args, &buf, old_sz, new_sz, mremap_mlock, &duration, &count) < 0) {
				(void)stress_munmap_retry_enomem(buf, old_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
			if (!stress_continue(args))
				goto deinit;
			(void)stress_madvise_random(buf, new_sz);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (stress_mmap_check(buf, new_sz, page_size) < 0) {
					pr_fail("%s: mremap'd region "
						"of %zu bytes does "
						"not contain expected data\n",
						args->name, sz);
					(void)stress_munmap_retry_enomem(buf, new_sz);
					ret = EXIT_FAILURE;
					goto deinit;
				}
			}
			old_sz = new_sz;
			new_sz >>= 1;
		}

		new_sz <<= 1;
		while (new_sz < mremap_bytes) {
			if (try_remap(args, &buf, old_sz, new_sz, mremap_mlock, &duration, &count) < 0) {
				(void)stress_munmap_retry_enomem(buf, old_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
			if (!stress_continue(args))
				goto deinit;
			(void)stress_madvise_random(buf, new_sz);
			old_sz = new_sz;
			new_sz <<= 1;
		}

		/* Invalid remap flags */
		ptr = mremap(buf, old_sz, old_sz, ~0);
		if (ptr && (ptr != MAP_FAILED))
			buf = ptr;
		ptr = mremap(buf, old_sz, old_sz, MREMAP_FIXED | MREMAP_MAYMOVE, (void *)~(uintptr_t)0);
		if (ptr && (ptr != MAP_FAILED))
			buf = ptr;
#if defined(MREMAP_MAYMOVE)
		/* Invalid new size */
		ptr = mremap(buf, old_sz, 0, MREMAP_MAYMOVE);
		if (ptr && (ptr != MAP_FAILED))
			buf = ptr;
#endif
		(void)stress_munmap_retry_enomem(buf, old_sz);

		stress_bogo_inc(args);
	} while (stress_continue(args));

deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mremap call", rate * STRESS_DBL_NANOSECOND);

	return ret;
}

/*
 *  stress_mremap()
 *	stress mmap
 */
static int stress_mremap(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mremap_child, STRESS_OOMABLE_NORMAL);
}

stressor_info_t stress_mremap_info = {
	.stressor = stress_mremap,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_mremap_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without mremap() system call support"
};
#endif
