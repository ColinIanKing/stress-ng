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
#include "core-mmap.h"
#include "core-shim.h"
#include "core-sort.h"

#if defined(HAVE_SEARCH_H)
#include <search.h>
#endif

typedef void * (*bsearch_func_t)(const void *key, const void *base, size_t nmemb, size_t size,
			       int (*compare)(const void *p1, const void *p2));

typedef struct {
	const char *name;
	const bsearch_func_t bsearch_func;
} stress_bsearch_method_t;

#define MIN_BSEARCH_SIZE	(1 * KB)
#define MAX_BSEARCH_SIZE	(64 * MB)
#define DEFAULT_BSEARCH_SIZE	(64 * KB)

static const stress_help_t help[] = {
	{ NULL,	"bsearch N",	  	"start N workers that exercise a binary search" },
	{ NULL,	"bsearch-method M",	"select bsearch method [ bsearch-libc | bsearch-nonlibc ]" },
	{ NULL,	"bsearch-ops N",  	"stop after N binary search bogo operations" },
	{ NULL,	"bsearch-size N", 	"number of 32 bit integers to bsearch" },
	{ NULL,	NULL,			NULL }
};

static void OPTIMIZE3 * bsearch_nonlibc(
	const void *key,
	const void *base,
	size_t nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register size_t lower = 0;
	register size_t upper = nmemb;

	while (LIKELY(lower < upper)) {
		register const size_t idx = (lower + upper) >> 1;
		register const void *ptr = (const char *)base + (idx * size);
		register const int cmp = compare(key, ptr);

		if (cmp < 0) {
			upper = idx;
		} else if (cmp > 0) {
			lower = idx + 1;
		} else {
			return shim_unconstify_ptr(ptr);
		}
	}
	return NULL;
}

static void OPTIMIZE3 * bsearch_ternary(
	const void *key,
	const void *base,
	size_t nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register size_t lower = 0;
	register size_t upper = nmemb;

	while (LIKELY(upper >= lower)) {
		register const size_t diff = upper - lower;
		register const size_t mid1 = lower + (diff / 3);
		register const size_t mid2 = upper - (diff / 3);
		register const void *ptr1, *ptr2;
		register int cmp1, cmp2;

		ptr1 = (const void *)((const char *)base + (mid1 * size));
		cmp1 = compare(key, ptr1);
		if (cmp1 == 0)
			return shim_unconstify_ptr(ptr1);

		ptr2 = (const void *)((const char *)base + (mid2 * size));
		cmp2 = compare(key, ptr2);
		if (cmp2 == 0)
			return shim_unconstify_ptr(ptr2);

		if (cmp1 < 0) {
			upper = mid1 - 1;
		} else if (cmp2 > 0) {
			lower = mid2 + 1;
		} else {
			lower = mid1 + 1;
			upper = mid2 - 1;
		}
	}
	return NULL;
}

static const stress_bsearch_method_t stress_bsearch_methods[] = {
#if defined(HAVE_BSEARCH)
	{ "bsearch-libc",	bsearch },
#endif
	{ "bsearch-nonlibc",	bsearch_nonlibc },
	{ "ternary",		bsearch_ternary },
};

static const char *stress_bsearch_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_bsearch_methods)) ? stress_bsearch_methods[i].name : NULL;
}

/*
 *  stress_bsearch()
 *	stress bsearch
 */
static int OPTIMIZE3 stress_bsearch(stress_args_t *args)
{
	int32_t *data, *ptr;
	size_t n, n8, i, bsearch_method = 0, data_size;
	uint64_t bsearch_size = DEFAULT_BSEARCH_SIZE;
	double rate, duration = 0.0, count = 0.0, sorted = 0.0;
	bsearch_func_t bsearch_func;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("bsearch-method", &bsearch_method);
	bsearch_func = stress_bsearch_methods[bsearch_method].bsearch_func;

	if (!stress_get_setting("bsearch-size", &bsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bsearch_size = MAX_BSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bsearch_size = MIN_BSEARCH_SIZE;
	}
	n = (size_t)bsearch_size;
	n8 = (n + 7) & ~7UL;
	data_size = n8 * sizeof(*data);

	/* allocate in multiples of 8 */
	data = (int32_t *)stress_mmap_populate(NULL,
				data_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zu bytes failed%s, errno=%d (%s), skipping stressor\n",
			args->name, data_size, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(data, data_size, "bsearch-data");

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

			result = bsearch_func(ptr, data, n, sizeof(*ptr), stress_sort_cmp_fwd_int32);
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
	stress_metrics_set(args, 0, "bsearch comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "bsearch comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f bsearch comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_bsearch_size,   "bsearch-size",   TYPE_ID_UINT64,        MIN_BSEARCH_SIZE, MAX_BSEARCH_SIZE, NULL },
	{ OPT_bsearch_method, "bsearch-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_bsearch_method },
	END_OPT,
};

const stressor_info_t stress_bsearch_info = {
	.stressor = stress_bsearch,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
