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
#include "core-mmap.h"
#include "core-shim.h"
#include "core-sort.h"

#define MIN_FIBSEARCH_SIZE	(1 * KB)
#define MAX_FIBSEARCH_SIZE	(64 * MB)
#define DEFAULT_FIBSEARCH_SIZE	(64 * KB)

static const stress_help_t help[] = {
	{ NULL,	"fibsearch N",	  	"start N workers that exercise a Fibonacci search" },
	{ NULL,	"fibsearch-ops N",  	"stop after N Fibonacci search bogo operations" },
	{ NULL,	"fibsearch-size N", 	"number of 32 bit integers to Fibonacci search" },
	{ NULL,	NULL,			NULL }
};

static void OPTIMIZE3 * fibsearch(
	const void *key,
	void *base,
	size_t nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register size_t fib2 = 0;
	register size_t fib1 = 1;
	register size_t fib0 = fib2 + fib1;
	register ssize_t n = nmemb - 1;
	register void *ptr;
	register int offset = -1;

	while (fib0 < nmemb) {
		fib2 = fib1;
		fib1 = fib0;
		fib0 = fib2 + fib1;
	}

	while (fib0 > 1) {
		register ssize_t m = offset + fib2;
		register ssize_t idx = (m < n) ? m : n;
		register int cmp;

		ptr = (char *)base + (idx * size);
		cmp = compare(ptr, key);

		if (cmp < 0) {
			fib0 = fib1;
			fib1 = fib2;
			fib2 = fib0 - fib1;
			offset = idx;
		} else if (cmp > 0) {
			fib0 = fib2;
			fib1 = fib1 - fib2;
			fib2 = fib0 - fib1;
		} else {
			return ptr;
		}
	}

	ptr = (char *)base + ((offset + 1) * size);
	if (fib1 && (compare(key, ptr) == 0))
		return ptr;

	return NULL;
}

/*
 *  stress_fibsearch()
 *	stress fibsearch
 */
static int OPTIMIZE3 stress_fibsearch(stress_args_t *args)
{
	int32_t *data, *ptr;
	size_t n, n8, i, data_size;
	uint64_t fibsearch_size = DEFAULT_FIBSEARCH_SIZE;
	double rate, duration = 0.0, count = 0.0, sorted = 0.0;
	int rc = EXIT_SUCCESS;

	if (!stress_get_setting("fibsearch-size", &fibsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			fibsearch_size = MAX_FIBSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			fibsearch_size = MIN_FIBSEARCH_SIZE;
	}
	n = (size_t)fibsearch_size;
	n8 = (n + 7) & ~7UL;
	data_size = n8 * sizeof(*data);

	/* allocate in multiples of 8 */
	data = (int32_t *)stress_mmap_populate(NULL,
				data_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
			args->name, data_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(data, data_size, "fibsearch-data");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;

		stress_sort_data_int32_init(data, n);
		stress_sort_compare_reset();
		t = stress_time_now();
		for (ptr = data, i = 0; i < n; i++, ptr++) {
			int32_t *result;

			result = fibsearch(ptr, data, n, sizeof(*ptr), stress_sort_cmp_fwd_int32);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu could not be found\n",
						args->name, i);
				else if (*result != *ptr) {
					pr_fail("%s: element %zu "
						"found %" PRIu32
						", expecting %" PRIu32 "\n",
						args->name, i, *result, *ptr);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)i;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "fibsearch comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "fibsearch comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f fibsearch comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_fibsearch_size,   "fibsearch-size",   TYPE_ID_UINT64,        MIN_FIBSEARCH_SIZE, MAX_FIBSEARCH_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_fibsearch_info = {
	.stressor = stress_fibsearch,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
