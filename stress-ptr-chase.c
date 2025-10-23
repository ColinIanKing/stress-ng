/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-mmap.h"

#define MIN_NEXT_PTRS_4K_PAGES		(64)
#define MAX_NEXT_PTRS_4K_PAGES		(256 * 1024)
#define DEFAULT_NEXT_PTRS_SIZE		(4096)
#define PAGE_SIZE_4K			(4096)

#define PTRS_PER_4K_PAGE		(PAGE_SIZE_4K / sizeof(void *))	/* Must be power of 2 */

static const stress_help_t help[] = {
	{ NULL,	"ptr-chase N",	 	"start N workers that chase pointers around many nodes" },
	{ NULL,	"ptr-chase-ops N",	"stop after N bogo pointer chase operations" },
	{ NULL,	"ptr-chase-pages N",	"N is the number of pages for nodes of pointers" },
	{ NULL,	NULL,		 	NULL }
};

typedef struct stress_ptrs {
	/* note that bottom bit is used to flag that ptr has been accessed */
	struct stress_ptrs *next[PTRS_PER_4K_PAGE];
} stress_ptrs_t;

static const stress_opt_t opts[] = {
	{ OPT_ptr_chase_pages, "ptr-chase-pages", TYPE_ID_UINT64, MIN_NEXT_PTRS_4K_PAGES, MAX_NEXT_PTRS_4K_PAGES, NULL },
	END_OPT,
};

/*
 *  stress_ptr_chase()
 *	stress list
 */
static int stress_ptr_chase(stress_args_t *args)
{
	uint64_t ptr_chase_pages = DEFAULT_NEXT_PTRS_SIZE;
	size_t n, i;
	int rc = EXIT_NO_RESOURCE;
	stress_ptrs_t **ptrs;
	stress_ptrs_t *ptrs_heap, *ptrs_mmap;
	register uintptr_t ptr_mask = ~(uintptr_t)1;
	register stress_ptrs_t *ptr;
	size_t ptrs_size, total = 0, visited = 0;
	size_t alloc_size;
	double metric, t_start, duration;
	uint64_t counter;

	if (!stress_get_setting("ptr-chase-pages", &ptr_chase_pages)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			ptr_chase_pages = MAX_NEXT_PTRS_4K_PAGES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			ptr_chase_pages = MIN_NEXT_PTRS_4K_PAGES;
	}

	n = (size_t)ptr_chase_pages;
	alloc_size = PAGE_SIZE_4K * ((n + 1) / 2);

	ptrs_heap = (stress_ptrs_t *)calloc(1, alloc_size);
	if (!ptrs_heap) {
		pr_inf("%s: failed to allocation heap of %zu bytes failed%s, "
			"skipping stressor\n",
			args->name, alloc_size, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	ptrs_mmap = (stress_ptrs_t *)stress_mmap_populate(NULL, alloc_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE,
					-1, 0);
	if (ptrs_mmap == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, n,
			stress_get_memfree_str(), errno, strerror(errno));
		goto tidy_ptrs_heap;
	}
	stress_set_vma_anon_name(ptrs_mmap, alloc_size, "pointer-nodes");

	ptrs_size = n * sizeof(*ptrs);
	ptrs = (stress_ptrs_t **)stress_mmap_populate(NULL, ptrs_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE,
					-1, 0);
	if (ptrs == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu pointer entries%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, n,
			stress_get_memfree_str(), errno, strerror(errno));
		goto tidy_ptrs_mmap;
	}
	stress_set_vma_anon_name(ptrs, ptrs_size, "pointers");

	if (stress_instance_zero(args))
		pr_dbg("%s using %zu x %zuK pages, %zu pointers\n",
			args->name, n, args->page_size >> 10, n * PTRS_PER_4K_PAGE);

	for (i = 0; i < n; i++) {
		register size_t j = i >> 1;

		ptrs[i] = (i & 1) ? &ptrs_heap[j] : &ptrs_mmap[j];
	}

	for (i = 0; i < n; i++) {
		register size_t j;

		ptr = ptrs[i];
		for (j = 0; j < PTRS_PER_4K_PAGE; j++) {
			register size_t k;
			do {
				k = (size_t)stress_mwc32modn((uint32_t)n);
			} while (k == i);

			ptr->next[j] = ptrs[k];
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ptr = ptrs[0];
	t_start = stress_time_now();
	do {
		register size_t j = stress_mwc16() & (PTRS_PER_4K_PAGE - 1);
		register uintptr_t addr = (uintptr_t)ptr->next[j];

		ptr->next[j] = (stress_ptrs_t *)(addr | 1);
		ptr = (stress_ptrs_t *)(addr & ptr_mask);

		stress_bogo_inc(args);
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

	for (i = 0; i < n; i++) {
		register size_t j;
		register uintptr_t addr = (uintptr_t)ptrs[i];

		ptr = (stress_ptrs_t *)(addr & ptr_mask);

		for (j = 0; j < PTRS_PER_4K_PAGE; j++) {
			visited += ((uintptr_t)ptr->next[j] & 1);
		}
		total += PTRS_PER_4K_PAGE;
	}

	metric = (total > 0) ? 100.0 * (double)visited / (double)total : 0.0;
	stress_metrics_set(args, 0, "% pointers chased", metric, STRESS_METRIC_HARMONIC_MEAN);

	counter = stress_bogo_get(args);
	metric = (counter > 0) ? (duration * STRESS_DBL_NANOSECOND) / (double)counter: 0.0;
	stress_metrics_set(args, 0, "nanosec per pointer", metric, STRESS_METRIC_HARMONIC_MEAN);

	if (metric > 0.0)
		pr_dbg("%s: %.2f pointers chased per second\n", args->name, 1.0E9 / metric);

	rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)ptrs, ptrs_size);

tidy_ptrs_mmap:
	(void)munmap((void *)ptrs_mmap, alloc_size);

tidy_ptrs_heap:
	free(ptrs_heap);

	return rc;
}

const stressor_info_t stress_ptr_chase_info = {
	.stressor = stress_ptr_chase,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
