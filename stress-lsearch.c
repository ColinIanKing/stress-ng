/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <search.h>

#include "stress-ng.h"

static uint64_t opt_lsearch_size = DEFAULT_LSEARCH_SIZE;
static bool set_lsearch_size = false;

/*
 *  stress_set_lsearch_size()
 *      set lsearch size from given option string
 */
void stress_set_lsearch_size(const char *optarg)
{
	set_lsearch_size = true;
	opt_lsearch_size = get_uint64_byte(optarg);
	check_range("lsearch-size", opt_lsearch_size,
		MIN_TSEARCH_SIZE, MAX_TSEARCH_SIZE);
}

/*
 *  cmp()
 *	lsearch uint32 comparison for sorting
 */
static int cmp(const void *p1, const void *p2)
{
	return (*(uint32_t *)p1 - *(uint32_t *)p2);
}

/*
 *  stress_lsearch()
 *	stress lsearch
 */
int stress_lsearch(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int32_t *data, *root;
	size_t i, max;

	(void)instance;

	if (!set_lsearch_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_lsearch_size = MAX_LSEARCH_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_lsearch_size = MIN_LSEARCH_SIZE;
	}
	max = (size_t)opt_lsearch_size;

	if ((data = calloc(max, sizeof(int32_t))) == NULL) {
		pr_fail_dbg(name, "malloc");
		return EXIT_FAILURE;
	}
	if ((root = calloc(max, sizeof(int32_t))) == NULL) {
		free(data);
		pr_fail_dbg(name, "malloc");
		return EXIT_FAILURE;
	}

	do {
		size_t n = 0;

		/* Step #1, populate with data */
		for (i = 0; opt_do_run && i < max; i++) {
			data[i] = ((mwc32() & 0xfff) << 20) ^ i;
			(void)lsearch(&data[i], root, &n, sizeof(int32_t), cmp);
		}
		/* Step #2, find */
		for (i = 0; opt_do_run && i < n; i++) {
			int32_t *result;

			result = lfind(&data[i], root, &n, sizeof(int32_t), cmp);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail(stderr, "%s: element %zu could not be found\n", name, i);
				else if (*result != data[i])
					pr_fail(stderr, "%s: element %zu found %" PRIu32 ", expecting %" PRIu32 "\n",
					name, i, *result, data[i]);
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	free(root);
	free(data);
	return EXIT_SUCCESS;
}
