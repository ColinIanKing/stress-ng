/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
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

	lsearch_size = get_uint64(opt);
	check_range("lsearch-size", lsearch_size,
		MIN_TSEARCH_SIZE, MAX_TSEARCH_SIZE);
	return set_setting("lsearch-size", TYPE_ID_UINT64, &lsearch_size);
}

/*
 *  cmp()
 *	lsearch uint32 comparison for sorting
 */
static int cmp(const void *p1, const void *p2)
{
	return (*(const uint32_t *)p1 - *(const uint32_t *)p2);
}

/*
 *  stress_lsearch()
 *	stress lsearch
 */
static int stress_lsearch(const args_t *args)
{
	int32_t *data, *root;
	size_t i, max;
	uint64_t lsearch_size = DEFAULT_LSEARCH_SIZE;

	if (!get_setting("lsearch-size", &lsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			lsearch_size = MAX_LSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			lsearch_size = MIN_LSEARCH_SIZE;
	}
	max = (size_t)lsearch_size;

	if ((data = calloc(max, sizeof(*data))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}
	if ((root = calloc(max, sizeof(*data))) == NULL) {
		free(data);
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	do {
		size_t n = 0;

		/* Step #1, populate with data */
		for (i = 0; g_keep_stressing_flag && i < max; i++) {
			void *ptr;

			data[i] = ((mwc32() & 0xfff) << 20) ^ i;
			ptr = lsearch(&data[i], root, &n, sizeof(*data), cmp);
			(void)ptr;
		}
		/* Step #2, find */
		for (i = 0; g_keep_stressing_flag && i < n; i++) {
			int32_t *result;

			result = lfind(&data[i], root, &n, sizeof(*data), cmp);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu could not be found\n", args->name, i);
				else if (*result != data[i])
					pr_fail("%s: element %zu found %" PRIu32 ", expecting %" PRIu32 "\n",
					args->name, i, *result, data[i]);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	free(root);
	free(data);
	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_lsearch_size,	stress_set_lsearch_size },
	{ 0,			NULL }
};

stressor_info_t stress_lsearch_info = {
	.stressor = stress_lsearch,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
