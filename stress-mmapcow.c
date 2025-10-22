/*
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-prime.h"

#define MMAPCOW_FORK	(0x0001)
#define MMAPCOW_FREE	(0x0002)
#define MMAPCOW_MLOCK	(0x0004)
#define MMAPCOW_NUMA	(0x0008)

static const stress_help_t help[] = {
	{ NULL,	"mmapcow N",      "start N workers stressing copy-on-write and munmaps" },
	{ NULL, "mmapcow-fork",   "force more page copying by reguler process forking" },
	{ NULL,	"mmapcow-free",	  "use madvise(MADV_FREE) on each page before unmapping" },
	{ NULL,	"mmapcow-mlock",  "lock copy-on-write page into memory before unmapping" },
	{ NULL,	"mmapcow-numa",	  "bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"mmapcow-ops N",  "stop after N mmapcow bogo operations" },
	{ NULL,	NULL,             NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mmapcow_fork,  "mmapcow-fork",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapcow_free,  "mmapcow-free",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapcow_mlock, "mmapcow-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapcow_numa,  "mmapcow-numa",  TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

#if defined(HAVE_MADVISE)

#if defined(HAVE_LINUX_MEMPOLICY_H)
static stress_numa_mask_t *numa_mask = NULL;
static stress_numa_mask_t *numa_nodes = NULL;
#endif

/*
 *   stress_mmapcow_force_unmap()
 *	a single page munmap() failed, this can occur because
 * 	there is no memory available to break vma and free a
 *	page, but there is hope, we can MADV_FREE the mapping
 *	and then ummap the entire buffer, this normally allows
 *	the unmapping
 */
static void stress_mmapcow_force_unmap(
	stress_args_t *args,
	uint8_t *buf,
	const size_t buf_size,
	const size_t page_size)
{
#if defined(MADV_DONTNEED)
	(void)madvise((void *)buf, buf_size, MADV_DONTNEED);
#endif
#if defined(MADV_FREE)
	(void)madvise((void *)buf, buf_size, MADV_FREE);
#endif
	if (munmap((void *)buf, buf_size) < 0) {
		pr_fail("%s: munmap of %zu pages failed, errno=%d (%s)\n",
			args->name, buf_size / page_size, errno, strerror(errno));
		return;
	}
}

/*
 *  stress_mmapcow_modify_unmap()
 *	modify page, unmap it. If unmap fails, force
 *	unmap of entire buffer
 */
static int OPTIMIZE3 stress_mmapcow_modify_unmap(
	stress_args_t *args,
	uint8_t *buf,
	const size_t buf_size,
	uint8_t *page,
	const size_t page_size,
	const int flags,
	double *duration,
	double *count)
{
	volatile uint8_t *ptr = page;
	const uint8_t *ptr_end = page + page_size;
	uint64_t val = stress_mwc64() | 0x1248124812481248ULL;	/* random, and never zero */
	double t1, t2;

	(void)flags;

#if defined(MADV_FREE)
	if (flags & MMAPCOW_FREE)
		(void)madvise((void *)page, page_size, MADV_FREE);
#endif
	/* Time duration of page touch + page fault */
	t1 = stress_time_now();
	*(volatile uint64_t *)ptr = val;
	t2 = stress_time_now();
PRAGMA_UNROLL_N(8)
	for (; ptr < ptr_end; ptr += 64) {
		*(volatile uint64_t *)ptr = val;
	}
	stress_cpu_data_cache_flush(page, page_size);
	(*duration) += (t2 - t1);
	(*count) += 1;

#if defined(MADV_FREE)
	if (flags & MMAPCOW_FREE)
		(void)madvise((void *)page, page_size, MADV_FREE);
#endif
	if (UNLIKELY(munmap((void *)page, page_size) < 0)) {
		if (errno == ENOMEM) {
			stress_mmapcow_force_unmap(args, buf, buf_size, page_size);
			return -1;
		}
		stress_mmapcow_force_unmap(args, buf, buf_size, page_size);
		pr_fail("%s: munmap of page at %p failed, errno=%d (%s)\n",
			args->name, page, errno, strerror(errno));
		return -1;
	}
	stress_bogo_inc(args);
	return 0;
}

/*
 *  stress_mmapcow_exercise()
 *	exercise mmap copy-on-write pages
 */
static int stress_mmapcow_exercise(
	stress_args_t *args,
	int *flags,
	double *duration,
	double *count,
	size_t *buf_size,
	size_t *max_buf_size,
	size_t *failed_size,
	int *failed_count)
{
	uint8_t *buf = NULL, *buf_end, *ptr;
	uint8_t rnd;
	size_t stride, n_pages, i, offset;
	pid_t pid = -1;
	const size_t page_size = args->page_size;
	const size_t page_size2 = page_size + page_size;
	unsigned char vec[1];

	n_pages = *buf_size / page_size;
	buf = (uint8_t *)mmap(NULL, *buf_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (buf == MAP_FAILED) {
		if (*buf_size == page_size) {
			pr_inf("%s: failed to mmap %zu bytes, errno=%d (%s), terminating early\n",
				args->name, *buf_size, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		*failed_size = *buf_size;
		*buf_size = page_size;
		*failed_count = 0;
		return EXIT_SUCCESS;
	}

#if defined(MCL_ONFAULT) &&	\
    defined(MCL_FUTURE)
	if (*flags & MMAPCOW_MLOCK) {
		if (shim_mlock2(buf, *buf_size, MCL_ONFAULT | MCL_FUTURE) < 0) {
			if (errno == ENOSYS)
				*flags &= ~(*flags) & MMAPCOW_MLOCK;
		}
	}
#endif
#if defined(MADV_COLLAPSE)
	(void)madvise((void *)buf, *buf_size, MADV_COLLAPSE);
#endif
#if defined(MADV_DONTNEED)
	(void)madvise((void *)buf, *buf_size, MADV_DONTNEED);
#endif
#if defined(MADV_MERGEABLE)
	(void)madvise((void *)buf, *buf_size, MADV_MERGEABLE);
#endif

	/* Low memory? Start again.. */
	if (stress_low_memory(64 * page_size)) {
		(void)munmap((void *)buf, *buf_size);
		*buf_size = page_size;
		return EXIT_SUCCESS;
	}
#if defined(HAVE_LINUX_MEMPOLICY_H)
	if ((*flags & MMAPCOW_NUMA) && numa_mask && numa_nodes)
		stress_numa_randomize_pages(args, numa_nodes, numa_mask, buf, *buf_size, page_size);
#endif
	stress_set_vma_anon_name(buf, *buf_size, "mmapcow-pages");
	buf_end = buf + *buf_size;
	rnd = stress_mwc8() & 7;

	if (*flags & MMAPCOW_FORK) {
		pid = fork();

		/* force child to have different rnd from parent */
		if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			rnd = stress_mwc8() & 7;

			/* don't exercise child on low memory scenarios */
			if (stress_low_memory(64 * page_size)) {
				(void)munmap((void *)buf, *buf_size);
				_exit(EXIT_SUCCESS);
			}
		}
	}

	switch (rnd) {
	case 0:
		/* Forward */
		for (ptr = buf; stress_continue(args) && (ptr < buf_end); ptr += page_size) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		break;
	case 1:
		/* Foward stride even pages then odd pages */
		for (ptr = buf; stress_continue(args) && (ptr < buf_end); ptr += page_size2) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		for (ptr = buf + page_size; stress_continue(args) && (ptr < buf_end); ptr += page_size2) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		break;
	case 2:
		/* Forward prime stride */
		stride = stress_get_prime64(n_pages) * page_size;
		for (i = 0, offset = 0; stress_continue(args) && i < n_pages; i++) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					buf + offset, page_size, *flags, duration, count) < 0))
				goto next;
			offset += stride;
			offset %= *buf_size;
		}
		break;
	case 3:
		/* Reverse */
		for (ptr = buf + *buf_size - page_size; stress_continue(args) && (ptr >= buf); ptr -= page_size) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		break;
	case 4:
		/* Reverse stride even pages then odd pages */
		for (ptr = buf + *buf_size - page_size; stress_continue(args) && (ptr >= buf); ptr -= page_size2) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		for (ptr = buf + *buf_size - page_size2; stress_continue(args) && (ptr >= buf); ptr -= page_size2) {
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		break;
	case 5:
		/* Randomly chosen pages, check if mincore works on buf */
		if (shim_mincore(buf, 1, vec) == 0) {
			for (i = 0; stress_continue(args) && i < n_pages; i++) {
				offset = stress_mwc64modn((uint64_t)n_pages) * page_size;

				/* is randomly chosen page mmapd? */
				if (shim_mincore(buf + offset, 1, vec) == 0) {
					if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
							buf + offset, page_size, *flags, duration, count) < 0)) {
						goto next;
					}
				}
			}
		}
		stress_mmapcow_force_unmap(args, buf, *buf_size, page_size);
		break;
	case 6:
		/* Populate 1 random page, unmap all */
		offset = stress_mwc64modn(n_pages) * page_size;
		(void)shim_memset(buf + offset, 0xff, page_size);
#if defined(MADV_FREE)
		if (*flags & MMAPCOW_FREE)
			(void)madvise((void *)(buf + offset), page_size, MADV_FREE);
#endif
		(void)munmap((void *)buf, *buf_size);
		stress_bogo_inc(args);
		break;
	case 7:
		for (i = 0, ptr = buf; stress_continue(args) && i < n_pages; i++, ptr += page_size) {
			offset = stress_mwc64modn((uint64_t)n_pages) * page_size;
#if defined(MADV_MERGEABLE) &&	\
    defined(MADV_UNMERGEABLE)
			/* Random mergeable advice */
			(void)madvise((void *)(buf + offset), page_size,
					stress_mwc1() ? MADV_MERGEABLE : MADV_UNMERGEABLE);
#endif
			/* Sequential page unmapping */
			if (UNLIKELY(stress_mmapcow_modify_unmap(args, buf, *buf_size,
					ptr, page_size, *flags, duration, count) < 0))
				goto next;
		}
		break;
	default:
		break;
	}
next:
	if (*buf_size > *max_buf_size)
		*max_buf_size = *buf_size;
	*buf_size = *buf_size + *buf_size;
	if (*buf_size >= *failed_size) {
		(*failed_count)++;
		/*
		 *  After avoiding the failed mmap size 16
		 *  times, try pushing the threshold up again
		 */
		if (*failed_count < 16) {
			*buf_size = page_size;
		} else {
			*failed_size = ~(size_t)0;
			*failed_count = 0;
		}
	}

	/* Handle unlikely wrap */
	if (UNLIKELY(*buf_size < page_size))
		*buf_size = page_size;

	if (*flags & MMAPCOW_FORK) {
		if (pid == 0)
			_exit(EXIT_SUCCESS);
		else if (pid > 0) {
			int status;

			(void)shim_waitpid(pid, &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

static int stress_mmapcow_child(stress_args_t *args, void *ctxt)
{
	size_t buf_size, max_buf_size = 0, failed_size;
	int failed_count = 0;
	const size_t page_size = args->page_size;
	char tmp[32];
	int ret, flags = *(int *)ctxt;
	double duration = 0.0, count = 0.0, rate;

	buf_size = page_size;
	failed_size = ~(size_t)0;

	do {
		ret = stress_mmapcow_exercise(args, &flags, &duration, &count,
			&buf_size, &max_buf_size, &failed_size, &failed_count);
	} while ((ret == EXIT_SUCCESS) && stress_continue(args));

	rate = (count > 0) ? STRESS_DBL_NANOSECOND * (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per page modification (includes page fault, estimated)", rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_uint64_to_str(tmp, sizeof(tmp), max_buf_size, 0, true);
	pr_dbg("%s: max mmap size: %zu x %zuK pages (%s)\n", args->name,
		max_buf_size / page_size, page_size >> 10, tmp);

	return EXIT_SUCCESS;
}

/*
 *  stress_mmapcow()
 *	stress mmap, Copy-on-Write and munmap
 */
static int stress_mmapcow(stress_args_t *args)
{
	int ret, flags = 0;
	bool mmapcow_fork = false;
	bool mmapcow_free = false;
	bool mmapcow_mlock = false;
	bool mmapcow_numa = false;

	(void)stress_get_setting("mmapcow-fork", &mmapcow_fork);
	(void)stress_get_setting("mmapcow-free", &mmapcow_free);
	(void)stress_get_setting("mmapcow-mlock", &mmapcow_mlock);
	(void)stress_get_setting("mmapcow-numa", &mmapcow_numa);

	if (mmapcow_fork)
		flags |= MMAPCOW_FORK;

	if (mmapcow_free) {
#if defined(MADV_FREE)
		flags |= MMAPCOW_FREE;
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mmapcow-free selected but madvise(MADV_FREE) not available, disabling option\n",
				args->name);
#endif
	}
	if (mmapcow_mlock) {
#if defined(MCL_ONFAULT) &&	\
    defined(MCL_FUTURE)
		flags |= MMAPCOW_MLOCK;
#else
		if (stress_instance_zero(args)) {
			pr_inf("%s: --mmapcow-mlock selected but mlock with MCL_ONFAULT and "
					"MCL_FUTURE not available, disabling option\n",
				args->name);
		}
#endif
	}

	if (mmapcow_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask, "--mmapcow-numa", &mmapcow_numa);
		flags |= mmapcow_numa ? MMAPCOW_NUMA : 0;
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mmapcow-numa selected but not supported by this system, disabling option\n",
				args->name);
#endif
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &flags, stress_mmapcow_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
	if (numa_nodes)
		stress_numa_mask_free(numa_nodes);
#endif
	return ret;
}

const stressor_info_t stress_mmapcow_info = {
	.stressor = stress_mmapcow,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help
};

#else

const stressor_info_t stress_mmapcow_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without madvise support"
};

#endif
