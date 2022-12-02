/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(HAVE_SEARCH_H)
#include <search.h>
#endif

#define LSEARCH_SIZE_SHIFT	(20)
#define MIN_LSEARCH_SIZE	(1 * KB)
#define MAX_LSEARCH_SIZE	(1U << LSEARCH_SIZE_SHIFT)	/* 1 MB */
#define DEFAULT_LSEARCH_SIZE	(8 * KB)

static const stress_help_t help[] = {
	{ NULL,	"lsearch N",	  "start N workers that exercise a linear search" },
	{ NULL,	"lsearch-ops N",  "stop after N linear search bogo operations" },
	{ NULL,	"lsearch-size N", "number of 32 bit integers to lsearch" },
	{ NULL, NULL,		  NULL }
};

/*
 *  stress_set_lsearch_size()
 *      set lsearch size from given option string
 */
static int stress_set_lsearch_size(const char *opt)
{
	uint64_t lsearch_size;

	lsearch_size = stress_get_uint64(opt);
	stress_check_range("lsearch-size", lsearch_size,
		MIN_LSEARCH_SIZE, MAX_LSEARCH_SIZE);
	return stress_set_setting("lsearch-size", TYPE_ID_UINT64, &lsearch_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_lsearch_size,	stress_set_lsearch_size },
	{ 0,			NULL }
};

#if defined(HAVE_LSEARCH)

/*
 *  stress_lsearch()
 *	stress lsearch
 */
static int stress_lsearch(const stress_args_t *args)
{
	int32_t *data, *root;
	size_t i, max;
	uint64_t lsearch_size = DEFAULT_LSEARCH_SIZE;

	if (!stress_get_setting("lsearch-size", &lsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			lsearch_size = MAX_LSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			lsearch_size = MIN_LSEARCH_SIZE;
	}
	max = (size_t)lsearch_size;

	if ((data = calloc(max, sizeof(*data))) == NULL) {
		pr_inf_skip("%s: malloc failed allocating %zd integers, "
			"out of memory, skipping stressor\n", args->name, max);
		return EXIT_NO_RESOURCE;
	}
	if ((root = calloc(max, sizeof(*data))) == NULL) {
		free(data);
		pr_inf_skip("%s: malloc failed allocating %zd integers , "
			"out of memory, skipping stressor\n", args->name, max);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t n = 0;

		/* Step #1, populate with data */
		for (i = 0; keep_stressing_flag() && i < max; i++) {
			data[i] = (int32_t)(((stress_mwc16() & 0xfff) << LSEARCH_SIZE_SHIFT) ^ i);
			VOID_RET(void *, lsearch(&data[i], root, &n, sizeof(*data), stress_sort_cmp_int32));
		}
		/* Step #2, find */
		for (i = 0; keep_stressing_flag() && i < n; i++) {
			int32_t *result;

			result = lfind(&data[i], root, &n, sizeof(*data), stress_sort_cmp_int32);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu could not be found\n", args->name, i);
				else if (*result != data[i])
					pr_fail("%s: element %zu found %" PRIu32 ", expecting %" PRIu32 "\n",
					args->name, i, *result, data[i]);
			}
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(root);
	free(data);
	return EXIT_SUCCESS;
}

stressor_info_t stress_lsearch_info = {
	.stressor = stress_lsearch,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

stressor_info_t stress_lsearch_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without libc lsearch() support"
};

#endif
