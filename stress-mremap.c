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
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

#define DEFAULT_MREMAP_BYTES	(256 * MB)
#define MIN_MREMAP_BYTES	(4 * KB)
#define MAX_MREMAP_BYTES	(MAX_MEM_LIMIT)

static const stress_help_t help[] = {
	{ NULL,	"mremap N",	  "start N workers stressing mremap" },
	{ NULL,	"mremap-bytes N", "mremap N bytes maximum for each stress iteration" },
	{ NULL, "mremap-mlock",	  "mlock remap pages, force pages to be unswappable" },
	{ NULL, "mremap-numa",	  "bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"mremap-ops N",	  "stop after N mremap bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mremap_bytes, "mremap-bytes", TYPE_ID_SIZE_T_BYTES_VM, MIN_MREMAP_BYTES, MAX_MREMAP_BYTES, NULL },
	{ OPT_mremap_mlock, "mremap-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mremap_numa,  "mremap-numa",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
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

	(void)stress_munmap_force(addr, sz);

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
	stress_args_t *args,
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
		if (UNLIKELY(!stress_continue_flag())) {
			(void)stress_munmap_force(*buf, old_sz);
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
					(void)stress_munmap_force(*buf, new_sz);
				*buf = newbuf;
			}
#endif

#if defined(HAVE_MLOCK)
			if (mremap_mlock && *buf)
				(void)shim_mlock(*buf, new_sz);
#else
			(void)mremap_mlock;
#endif
			if (UNLIKELY(metrics_counter++ > 500))
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

static int stress_mremap_child(stress_args_t *args, void *context)
{
	size_t new_sz, sz, mremap_bytes, mremap_bytes_total = DEFAULT_MREMAP_BYTES;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
	const size_t page_size = args->page_size;
	bool mremap_mlock = false;
	bool mremap_numa = false;
	double duration = 0.0, count = 0.0, rate;
	int ret = EXIT_SUCCESS;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask = NULL;
	stress_numa_mask_t *numa_nodes = NULL;
#endif

#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif
	(void)context;

	if (!stress_get_setting("mremap-bytes", &mremap_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mremap_bytes_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mremap_bytes_total = MIN_MREMAP_BYTES;
	}
	mremap_bytes = mremap_bytes_total / args->instances;
	if (mremap_bytes < MIN_MREMAP_BYTES)
		mremap_bytes = MIN_MREMAP_BYTES;
	if (mremap_bytes < page_size)
		mremap_bytes = page_size;
	mremap_bytes_total = args->instances * mremap_bytes;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, mremap_bytes, mremap_bytes_total);

	new_sz = sz = mremap_bytes & ~(page_size - 1);

	(void)stress_get_setting("mremap-mlock", &mremap_mlock);
	(void)stress_get_setting("mremap-numa", &mremap_numa);

	if (mremap_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes,
						&numa_mask, "--mremap-numa",
						&mremap_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mremap-numa selected but not supported by this system, disabling option\n",
				args->name);
		mremap_numa = false;
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint8_t *buf = NULL, *ptr;
		size_t old_sz;

		if (UNLIKELY(!stress_continue_flag()))
			goto deinit;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#if defined(MAP_POPULATE)
			flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
#if defined(HAVE_LINUX_MEMPOLICY_H)
		if (mremap_numa)
			stress_numa_randomize_pages(args, numa_nodes, numa_mask, buf, sz, page_size);
#endif
		(void)stress_madvise_randomize(buf, new_sz);
		(void)stress_madvise_mergeable(buf, new_sz);
		(void)stress_mincore_touch_pages(buf, mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			stress_mmap_set(buf, new_sz, page_size);
			if (UNLIKELY(stress_mmap_check(buf, sz, page_size) < 0)) {
				pr_fail("%s: mmap'd region of %zu "
					"bytes does not contain expected data\n",
					args->name, sz);
				(void)stress_munmap_force(buf, new_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
		}

		old_sz = new_sz;
		new_sz >>= 1;
		while (new_sz > page_size) {
			if (UNLIKELY(try_remap(args, &buf, old_sz, new_sz, mremap_mlock, &duration, &count) < 0)) {
				(void)stress_munmap_force(buf, old_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
			if (UNLIKELY(!stress_continue(args)))
				goto deinit;
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (mremap_numa)
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, buf, page_size, new_sz);
#endif
			(void)stress_madvise_randomize(buf, new_sz);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (UNLIKELY(stress_mmap_check(buf, new_sz, page_size) < 0)) {
					pr_fail("%s: mremap'd region "
						"of %zu bytes does "
						"not contain expected data\n",
						args->name, sz);
					(void)stress_munmap_force(buf, new_sz);
					ret = EXIT_FAILURE;
					goto deinit;
				}
			}
			old_sz = new_sz;
			new_sz >>= 1;
		}

		new_sz <<= 1;
		while (new_sz < mremap_bytes) {
			if (UNLIKELY(try_remap(args, &buf, old_sz, new_sz, mremap_mlock, &duration, &count) < 0)) {
				(void)stress_munmap_force(buf, old_sz);
				ret = EXIT_FAILURE;
				goto deinit;
			}
			if (UNLIKELY(!stress_continue(args)))
				goto deinit;
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (mremap_numa)
				stress_numa_randomize_pages(args, numa_nodes, numa_mask, buf, page_size, new_sz);
#endif
			(void)stress_madvise_randomize(buf, new_sz);
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
		(void)stress_munmap_force(buf, old_sz);

		stress_bogo_inc(args);
	} while (stress_continue(args));

deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mremap call",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif

	return ret;
}

/*
 *  stress_mremap()
 *	stress mmap
 */
static int stress_mremap(stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mremap_child, STRESS_OOMABLE_NORMAL);
}

const stressor_info_t stress_mremap_info = {
	.stressor = stress_mremap,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
const stressor_info_t stress_mremap_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without mremap() system call support"
};
#endif
