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
#include "core-sort.h"
#include "core-builtin.h"

#if defined(HAVE_SEARCH_H) &&	\
    defined(HAVE_LSEARCH)
#include <search.h>
#endif

typedef void * (*lfind_func_t)(const void *key, const void *base, size_t *nmemb, size_t size,
				int (*compare)(const void *p1, const void *p2));
typedef void * (*lsearch_func_t)(const void *key, void *base, size_t *nmemb, size_t size,
				int (*compare)(const void *p1, const void *p2));

typedef struct {
	const char *name;
	const lfind_func_t lfind_func;
	const lsearch_func_t lsearch_func;
} stress_lsearch_method_t;

#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(64 * MB)
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

static const stress_help_t help[] = {
	{ NULL,	"lsearch N",		"start N workers that exercise a linear search" },
	{ NULL,	"lsearch-method M",	"select lsearch method [ lsearch-libc | lsearch-nonlibc ]" },
	{ NULL,	"lsearch-ops N",	"stop after N linear search bogo operations" },
	{ NULL,	"lsearch-size N",	"number of 32 bit integers to lsearch" },
	{ NULL, NULL,			NULL }
};

static inline void OPTIMIZE3 * lfind_nonlibc(
	const void *key,
	const void *base,
	size_t *nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register size_t i = 0;
	register const char *found = base;

	while ((i < *nmemb) && ((*compare)(key, (const void *)found) != 0)) {
		i++;
		found += size;
	}
	return (i < *nmemb) ? (void *)shim_unconstify_ptr(found) : NULL;
}

static void * OPTIMIZE3 lsearch_nonlibc(
	const void *key,
	void *base,
	size_t *nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register void *result = lfind_nonlibc(key, base, nmemb, size, compare);

	if (!result) {
		result = shim_memcpy((char *)base + ((*nmemb) * size), key, size);
		++(*nmemb);
	}
	return result;
}

static inline void OPTIMIZE3 * lfind_sentinel(
	const void *key,
	const void *base,
	size_t *nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	char *ptr = (char *)shim_unconstify_ptr(base);
	register char *ptr_end;
	char tmp[size];

	if (*nmemb < 1)
		return NULL;

	ptr_end = (char *)shim_unconstify_ptr(base) + (((*nmemb) - 1) * size);

	/* save last value */
	shim_memcpy((void *)tmp, ptr_end, size);
	/* copy key to last value */
	shim_memcpy((void *)ptr_end, key, size);

	/* compare until we reach end value */
	while (compare(ptr, key))
		ptr += size;

	/* copy saved last value back */
	shim_memcpy((void *)ptr_end, tmp, size);
	return ((ptr < ptr_end) || (compare(ptr_end, key) == 0)) ? ptr : NULL;
}

static void * OPTIMIZE3 lsearch_sentinel(
	const void *key,
	void *base,
	size_t *nmemb,
	size_t size,
	int (*compare)(const void *p1, const void *p2))
{
	register void *result = lfind_sentinel(key, base, nmemb, size, compare);

	if (!result) {
		result = shim_memcpy((char *)base + ((*nmemb) * size), key, size);
		++(*nmemb);
	}
	return result;
}

static const stress_lsearch_method_t stress_lsearch_methods[] = {
#if defined(HAVE_SEARCH_H) &&	\
    defined(HAVE_LSEARCH)
	{ "lsearch-libc",	lfind,		lsearch },
#endif
	{ "lsearch-nonlibc",	lfind_nonlibc,	lsearch_nonlibc },
	{ "lsearch-sentinel",	lfind_sentinel,	lsearch_sentinel },
};

static const char *stress_lsearch_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_lsearch_methods)) ? stress_lsearch_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_lsearch_method, "lsearch-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_lsearch_method },
	{ OPT_lsearch_size,   "lsearch-size",   TYPE_ID_UINT64, MIN_LSEARCH_SIZE, MAX_LSEARCH_SIZE, NULL },
	END_OPT,
};

static int32_t OPTIMIZE3 stress_lsearch_cmp_int32(const void *p1, const void *p2)
{
	register const int32_t v1 = *(const int32_t *)p1;
	register const int32_t v2 = *(const int32_t *)p2;

	stress_sort_compares++;
	return (v1 != v2);
}

/*
 *  stress_lsearch()
 *	stress lsearch
 */
static int stress_lsearch(stress_args_t *args)
{
	int32_t *data, *root;
	size_t i, max, lsearch_method = 0;
	uint64_t lsearch_size = DEFAULT_LSEARCH_SIZE;
	double rate, duration = 0.0, count = 0.0, sorted = 0.0;
	lsearch_func_t lsearch_func;
	lfind_func_t lfind_func;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("lsearch-method", &lsearch_method);
	lfind_func = stress_lsearch_methods[lsearch_method].lfind_func;
	lsearch_func = stress_lsearch_methods[lsearch_method].lsearch_func;

	if (!stress_get_setting("lsearch-size", &lsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			lsearch_size = MAX_LSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			lsearch_size = MIN_LSEARCH_SIZE;
	}
	max = (size_t)lsearch_size;

	if ((data = (int32_t *)calloc(max, sizeof(*data))) == NULL) {
		pr_inf_skip("%s: malloc failed allocating %zu integers%s, "
			"skipping stressor\n",
			args->name, max, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	if ((root = (int32_t *)calloc(max, sizeof(*data))) == NULL) {
		free(data);
		pr_inf_skip("%s: malloc failed allocating %zu integers%s, "
			"skipping stressor\n",
			args->name, max, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	stress_sort_data_int32_init(data, max);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;
		size_t n = 0;

		stress_sort_data_int32_shuffle(data, max);

		/* Step #1, populate with data */
		for (i = 0; LIKELY(stress_continue_flag() && (i < max)); i++) {
			VOID_RET(void *, lsearch_func(&data[i], root, &n, sizeof(*data), stress_lsearch_cmp_int32));
		}
		/* Step #2, find */
		stress_sort_compare_reset();
		t = stress_time_now();
		for (i = 0; LIKELY(stress_continue_flag() && (i < n)); i++) {
			int32_t *result;

			result = lfind_func(&data[i], root, &n, sizeof(*data), stress_lsearch_cmp_int32);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL) {
					pr_fail("%s: element %zu could not be found\n", args->name, i);
					rc = EXIT_FAILURE;
				} else if (*result != data[i]) {
					pr_fail("%s: element %zu found %" PRIu32 ", expecting %" PRIu32 "\n",
						args->name, i, *result, data[i]);
					rc = EXIT_FAILURE;
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
	stress_metrics_set(args, 0, "lsearch comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "lsearch comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f lsearch comparisons per sec\n", args->name, rate);

	free(root);
	free(data);
	return rc;
}

const stressor_info_t stress_lsearch_info = {
	.stressor = stress_lsearch,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
