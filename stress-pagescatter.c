/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-memory.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-put.h"

#include <math.h>

#define MAX_SCATTER_PAGES_LOG2		(30)
#define DEFAULT_SCATTER_PAGES_LOG2	(10)

/* different page scattered activity, keep in sync with scatter_types[] */
#define SCATTER_MMAP			(0)
#define SCATTER_READ			(1)
#define SCATTER_WRITE			(2)
#define SCATTER_MPROTECT		(3)
#define SCATTER_MUNMAP			(4)
#define SCATTER_MAX			(5)

/* per scatter run duration and activity count */
typedef struct rate {
	double duration[SCATTER_MAX];
	double count[SCATTER_MAX];
} rate_t;

typedef struct {
	void **pages;		/* mmap'd pages */
	size_t pages_sz;	/* size of pages mapping */
	size_t n_pages;		/* number of pages = 2^order */
	size_t order;		/* log2 of n_pages */
	size_t mapped;		/* number of pages successfully mmap'd */
	bool populate;		/* true - use MAP_POPULATE */
	/*
	 *  rates[n] represent times of 0..2^n pages
	 */
	rate_t rate[MAX_SCATTER_PAGES_LOG2 + 1];
} scatter_page_info_t;

static const stress_help_t help[] = {
	{ NULL,	"pagescatter N",        "start N workers that allocate pages at random addresses" },
	{ NULL,	"pagescatter-ops N",	"stop after N page operations" },
	{ NULL, "pagescatter-order N",	"log2 number of pages to use" },
	{ NULL, "pagescatter-populate", "prefault pages during page mapping" },
	{ NULL,	NULL,			NULL }
};

/* scatter types, keep in sync with SCATTER_* */
static const char *const scatter_types[] = {
	"mmap",
	"read",
	"write",
	"mprotect",
	"munmap",
};

static const stress_opt_t opts[] = {
	{ OPT_pagescatter_order,    "pagescatter-order",    TYPE_ID_SIZE_T, 0, MAX_SCATTER_PAGES_LOG2, NULL },
	{ OPT_pagescatter_populate, "pagescatter-populate", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

/*
 *  stress_pagescatter_mmap()
 *	mmap a page in a new random location
 *	that's not been mmap'd before
 */
static void OPTIMIZE3 *stress_pagescatter_mmap(
	const size_t page_size,
	const uintptr_t mask,
	const bool populate,
	double *duration)
{
	void *ptr;
	const bool addr32 = sizeof(uintptr_t) <= 4;
	const int shift = addr32 ?  12 : 18;

	do {
		static uintptr_t addr;
		unsigned char vec[1];
		double t;
		int flags = MAP_SHARED | MAP_ANONYMOUS;

#if defined(MAP_POPULATE)
		if (populate)
			flags |= MAP_POPULATE;
#else
		(void)populate;
#endif

		/* random address hint */
		addr = (addr32 ? (stress_mwc32() >> shift) :
				 (stress_mwc64() >> shift)) & mask;
#if defined(MAP_FIXED_NOREPLACE)
		t = stress_time_now();
		ptr = mmap((void *)addr, page_size, PROT_READ | PROT_WRITE,
			  flags | MAP_FIXED_NOREPLACE, -1, 0);
		if (ptr != MAP_FAILED) {
			*duration = stress_time_now() - t;
			return ptr;
		}
#endif
		errno = 0;

		/* if page is resident, it's mapped, try again  */
		if (shim_mincore((void *)addr, page_size, vec) == 0)
			continue;
		/* if addr can be madvised then it's mapped, try again */
		if  (shim_madvise((void *)addr, page_size, MADV_NORMAL) == 0)
			continue;
		/* if errno == ENOMEM it maybe not mapped */
		if (errno != ENOMEM)
			continue;
		/* now do an expensive pipe page check */
		if (stress_memory_readable((void *)addr, page_size))
			continue;

#if defined(MAP_FIXED)
		/* page is not accessable, so map it in */
		t = stress_time_now();
		ptr = mmap((void *)addr, page_size, PROT_READ | PROT_WRITE,
			  flags | MAP_FIXED, -1, 0);
		if (ptr != MAP_FAILED) {
			*duration = stress_time_now() - t;
			return ptr;
		}
#endif
		/* no MAX_FIXED or it failed, try addr as a hint */
		t = stress_time_now();
		ptr = mmap((void *)addr, page_size, PROT_READ | PROT_WRITE,
			  flags, -1, 0);
		if (ptr != MAP_FAILED) {
			*duration = stress_time_now() - t;
			return ptr;
		}
	} while (stress_continue_flag());

	*duration = 0.0;
	return MAP_FAILED;
}

/*
 *  stress_pagescatter_pages_read()
 *	read access the pages
 */
static inline OPTIMIZE3 void stress_pagescatter_pages_read(
	const size_t idx,
	scatter_page_info_t *info,
	const size_t n_pages,
	const size_t page_size)
{
	size_t i;
	size_t count = 0;
	double t;
	size_t n = page_size >> 3;

	t = stress_time_now();
	for (i = 0; i < n_pages; i++) {
		volatile const uint64_t *ptr = (volatile const uint64_t *)info->pages[i];

		if (ptr != MAP_FAILED) {
			register volatile const uint64_t *ptr_end = info->pages[i] + n;

PRAGMA_UNROLL
			while (ptr < ptr_end) {
				(void)ptr[0];
				(void)ptr[1];
				(void)ptr[2];
				(void)ptr[3];
				(void)ptr[4];
				(void)ptr[5];
				(void)ptr[6];
				(void)ptr[7];
				ptr += 8;
			}
			count++;
		}
	}
	info->rate[idx].duration[SCATTER_READ] += stress_time_now() - t;
	info->rate[idx].count[SCATTER_READ] += count;
}

/*
 *  stress_pagescatter_pages_write()
 *	write access the pages
 */
static inline OPTIMIZE3 void stress_pagescatter_pages_write(
	const size_t idx,
	scatter_page_info_t *info,
	const size_t n_pages,
	const size_t page_size)
{
	size_t i;
	size_t count = 0;
	double t;
	size_t n = page_size >> 3;
	uint64_t val = 0x8040201008040201ULL;

	t = stress_time_now();
	for (i = 0; i < n_pages; i++) {
		register uint64_t *ptr = info->pages[i];

		if (ptr != MAP_FAILED) {
			register const uint64_t *ptr_end = info->pages[i] + n;

PRAGMA_UNROLL
			while (ptr < ptr_end) {
				ptr[0] = val;
				ptr[1] = val;
				ptr[2] = val;
				ptr[3] = val;
				ptr[4] = val;
				ptr[5] = val;
				ptr[6] = val;
				ptr[7] = val;
				ptr += 8;
			}
			count++;
			val++;
		}
	}
	info->rate[idx].duration[SCATTER_WRITE] += stress_time_now() - t;
	info->rate[idx].count[SCATTER_WRITE] += count;
}

/*
 *  stress_pagescatter_pages_mprotect()
 *	change memory protection of pages
 */
static inline OPTIMIZE3 void stress_pagescatter_pages_mprotect(
	const size_t idx,
	scatter_page_info_t *info,
	const size_t n_pages,
	const size_t page_size)
{
	size_t i;
	size_t count = 0;
	double t;

	t = stress_time_now();
	for (i = 0; i < n_pages; i++) {
		void *ptr = info->pages[i];

		if ((ptr != MAP_FAILED) && (mprotect(ptr, page_size, PROT_NONE) == 0))
			count++;
	}
	info->rate[idx].duration[SCATTER_MPROTECT] += stress_time_now() - t;
	info->rate[idx].count[SCATTER_MPROTECT] += count;
}

/*
 *  stress_pagescatter_pages()
 *	exercise pages
 */
static size_t OPTIMIZE3 stress_pagescatter_pages(
	stress_args_t *args,
	const size_t idx,
	scatter_page_info_t *info)
{
	const size_t n_pages = 1U << idx;
	const size_t page_size = args->page_size;
	const uintptr_t mask = ~(page_size - 1);
	size_t i;
	double duration;
	size_t count;
	double t;
	const bool populate = info->populate;
	bool stress_pages = true;

	duration = 0.0;
	count = 0;
	for (i = 0; i < n_pages; i++) {
		double page_duration;

		if (stress_continue(args)) {
			info->pages[i] = stress_pagescatter_mmap(page_size, mask, populate, &page_duration);
			if (info->pages[i] != MAP_FAILED) {
				duration += page_duration;
				count++;
				stress_bogo_inc(args);
			}
		} else {
			info->pages[i] = MAP_FAILED;
			stress_pages = false;
		}
	}
	info->mapped = count;
	info->rate[idx].duration[SCATTER_MMAP] += duration;
	info->rate[idx].count[SCATTER_MMAP] += (double)count;

	if (stress_pages && info->mapped) {
		stress_pagescatter_pages_write(idx, info, n_pages, page_size);
		stress_pagescatter_pages_read(idx, info, n_pages, page_size);
		stress_pagescatter_pages_mprotect(idx, info, n_pages, page_size);
	}
	if (info->mapped) {
		duration = 0.0;
		count = 0;
		t = stress_time_now();
		for (i = 0; i < n_pages; i++) {
			if (info->pages[i] != MAP_FAILED)
				count += (munmap(info->pages[i], page_size) == 0);
			info->pages[i] = MAP_FAILED;
		}
		duration += stress_time_now() - t;
		info->rate[idx].duration[SCATTER_MUNMAP] += duration;
		info->rate[idx].count[SCATTER_MUNMAP] += (double)count;
	}

	return info->mapped;
}

/*
 *  stress_pagescatter_child()
 *	work through page orders from size 0..info_order, allocating
 *	2^0..2^n pages respectively
 */
static int OPTIMIZE3 stress_pagescatter_child(stress_args_t *args, void *context)
{
	scatter_page_info_t *info = (scatter_page_info_t *)context;
	size_t i;
	size_t max_loops = 1U << info->order;
	bool mapped = false;

	do {
		if (UNLIKELY(!stress_continue(args)))
			break;

		for (i = 0; i <= info->order; i++) {
			size_t n_loops = (size_t)sqrt((double)max_loops) / (1U << i);
			size_t j;

			if (n_loops < 1)
				n_loops = 1;

			for (j = 0; (j < n_loops) && stress_continue_flag(); j++) {
				mapped |= (stress_pagescatter_pages(args, i, info) > 0);
			}
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
		if (!mapped) {
			pr_inf_skip("%s: failed to mmap any pages, skipping stressor\n", args->name);
			return EXIT_NO_RESOURCE;
		}
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_pagescatter_no_populate()
 *	report MAP_POPULATE not supported
 */
static void stress_pagescatter_no_populate(
	stress_args_t *args,
	scatter_page_info_t *info)
{
	if (info->populate && stress_instance_zero(args))
		pr_inf("%s: MAP_POPULATE not supported, disabling "
		       "option pagescatter-populate\n", args->name);
	info->populate = false;
}

/*
 *  stress_pagescatter()
 *	stress mmap
 */
static int stress_pagescatter(stress_args_t *args)
{
	stress_memory_info_t meminfo;
	scatter_page_info_t *info;
	char buffer[120];
	char tmp[30];
	char str_used[32];
	char str_free[32];
	const char *str_free_ptr;
	const char *str_msg;
	size_t i;
	size_t n_pages;
	size_t max_order;
	int rc;

	info = stress_mmap_populate(NULL, sizeof(*info), PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (info == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed allocating %zu bytes, errno=%d (%s), skipping stressor\n",
			args->name, sizeof(info), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	info->order = DEFAULT_SCATTER_PAGES_LOG2;
	info->populate = false;
	if (!stress_setting_get("pagescatter-order", &info->order)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			info->order = MAX_SCATTER_PAGES_LOG2;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			info->order = 0;

	}
	(void)stress_setting_get("pagescatter-populate", &info->populate);

	/*
	 *   work within the free memory limits
	 */
	(void)stress_memory_info_get(&meminfo);
	max_order = 0;

	if ((meminfo.freemem > 0) && (args->instances > 0) && (args->page_size > 0)) {
		const size_t pages = meminfo.freemem / (args->page_size * args->instances);

		if (pages > 0)
			max_order = stress_log2(pages);
	}
	str_free_ptr = (meminfo.freemem == 0) ? "unknown" :
		stress_uint64_to_str(str_free, sizeof(str_free), (uint64_t)meminfo.freemem, 2, true);
	if (max_order && (info->order > max_order)) {
		info->order = max_order;
		str_msg = "limiting page order to";
	} else {
		str_msg= "using page order of";
	}
	/* ..and inform user */
	if (stress_instance_zero(args)) {
		const char *str_used_ptr = stress_uint64_to_str(str_used,
				sizeof(str_used), args->page_size * (1ULL << info->order), 2, true);
		pr_inf("%s: %s %zu (%s) per instance, have %s free memory\n",
			args->name, str_msg, info->order, str_used_ptr, str_free_ptr);
	}

#if !defined(MAP_POPULATE)
	stress_pagescatter_no_populate(args, info);
#else
	if (info->populate) {
		void *page;

		errno = 0;
		page = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
		if (page == MAP_FAILED) {
			if (errno == EINVAL)
				stress_pagescatter_no_populate(args, info);
		} else {
			(void)munmap(page, args->page_size);
		}
	}
#endif

	n_pages = 1U << info->order;

	info->pages_sz = sizeof(void *) * n_pages;
	info->pages = stress_mmap_populate(NULL, info->pages_sz, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1 , 0);
	if (info->pages == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed allocating %zu bytes, errno=%d (%s), skipping stressor\n",
			args->name, info->pages_sz, errno, strerror(errno));
		(void)munmap((void *)info, sizeof(*info));
		return EXIT_NO_RESOURCE;
	}

	for (i = 0; i <= info->order; i++) {
		size_t j;

		for (j = 0; j < SCATTER_MAX; j++) {
			info->rate[i].duration[j] = 0.0;
			info->rate[i].count[j] = 0.0;
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, info, stress_pagescatter_child, STRESS_OOMABLE_NORMAL);

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	/* # pages field is 10 chars wide for 2^30 pages */
	(void)snprintf(buffer, sizeof(buffer), "%10s", "# pages");
	for (i = 0; i < SCATTER_MAX; i++) {
		(void)snprintf(tmp, sizeof(tmp), " %10s", scatter_types[i]);
		(void)shim_strlcat(buffer, tmp, sizeof(buffer));
	}

	if (stress_instance_zero(args)) {
		pr_block_begin();
		pr_inf("%s: page operations per second:\n", args->name);
		pr_inf("%s: %s\n", args->name, buffer);
		for (i = 0; i <= info->order; i++) {
			size_t j;

			(void)snprintf(buffer, sizeof(buffer), "%10d", 1U << i);
			for (j = 0; j < SCATTER_MAX; j++) {
				if (info->rate[i].duration[j] > 0.0) {
					double rate = info->rate[i].duration[j] > 0.0 ?
						      info->rate[i].count[j] / info->rate[i].duration[j] : 0.0;
					(void)snprintf(tmp, sizeof(tmp), " %10.0f", rate);
				} else {
					(void)snprintf(tmp, sizeof(tmp), " %10s", "untested");
				}
				(void)shim_strlcat(buffer, tmp, sizeof(buffer));
			}
			pr_inf("%s: %s\n", args->name, buffer);
		}
		pr_block_end();
	}

	(void)munmap((void *)info->pages, info->pages_sz);
	(void)munmap((void *)info, sizeof(*info));

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("d-cache-read-miss"),
	STRESS_EX_FEATURE("d-cache-miss"),
	STRESS_EX_FEATURE("d-cache-write-miss"),
	STRESS_EX_FEATURE("maple-tree-write"),
	STRESS_EX_FEATURE("mmap-lock"),
	STRESS_EX_FEATURE("page-faults-major"),
	STRESS_EX_FEATURE("page-faults-minor"),
	STRESS_EX_FEATURE("page-faults-user"),
	STRESS_EX_FEATURE("tlb"),
	STRESS_EX_FEATURE("tlb-flush"),
	STRESS_EX_FEATURE("system-time"),

	STRESS_EX_SYSCALL("mincore"),
	STRESS_EX_SYSCALL("mmap"),
	STRESS_EX_SYSCALL("mprotect"),
	STRESS_EX_SYSCALL("munap"),

	STRESS_EX_END,
};

const stressor_info_t stress_pagescatter_info = {
	.stressor = stress_pagescatter,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.exercises = exercises,
};
