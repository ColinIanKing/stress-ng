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

static uint64_t opt_tsearch_size = DEFAULT_TSEARCH_SIZE;
static bool set_tsearch_size = false;

/*
 *  stress_set_tsearch_size()
 *      set tsearch size from given option string
 */
void stress_set_tsearch_size(const char *optarg)
{
	set_tsearch_size = true;
	opt_tsearch_size = get_uint64_byte(optarg);
	check_range("tsearch-size", opt_tsearch_size,
		MIN_TSEARCH_SIZE, MAX_TSEARCH_SIZE);
}

/*
 *  cmp()
 *	sort on int32 values
 */
static int cmp(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

/*
 *  stress_tsearch()
 *	stress tsearch
 */
int stress_tsearch(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int32_t *data;
	size_t i, n;

	(void)instance;

	if (!set_tsearch_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_tsearch_size = MAX_TSEARCH_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_tsearch_size = MIN_TSEARCH_SIZE;
	}
	n = (size_t)opt_tsearch_size;

	if ((data = malloc(sizeof(int32_t) * n)) == NULL) {
		pr_fail_dbg(name, "malloc");
		return EXIT_FAILURE;
	}

	do {
		void *root = NULL;

		/* Step #1, populate tree */
		for (i = 0; i < n; i++) {
			data[i] = ((mwc32() & 0xfff) << 20) ^ i;
			if (tsearch(&data[i], &root, cmp) == NULL) {
				size_t j;

				pr_err(stderr, "%s: cannot allocate new "
					"tree node\n", name);
				for (j = 0; j < i; j++)
					tdelete(&data[j], &root, cmp);
				goto abort;
			}
		}
		/* Step #2, find */
		for (i = 0; opt_do_run && i < n; i++) {
			void **result;

			result = tfind(&data[i], &root, cmp);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail(stderr, "%s: element %zu "
						"could not be found\n",
						name, i);
				else {
					int32_t *val;
					val = *result;
					if (*val != data[i])
						pr_fail(stderr, "%s: element "
							"%zu found %" PRIu32
							", expecting %" PRIu32 "\n",
							name, i, *val, data[i]);
				}
			}
		}
		/* Step #3, delete */
		for (i = 0; i < n; i++) {
			void **result;

			result = tdelete(&data[i], &root, cmp);
			if ((opt_flags & OPT_FLAGS_VERIFY) && (result == NULL))
				pr_fail(stderr, "%s: element %zu could not "
					"be found\n", name, i);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	free(data);
	return EXIT_SUCCESS;
}
