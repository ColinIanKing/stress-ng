/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

static uint64_t opt_bsearch_size = DEFAULT_BSEARCH_SIZE;
static bool set_bsearch_size = false;

/*
 *  stress_set_bsearch_size()
 *	set bsearch size from given option string
 */
void stress_set_bsearch_size(const char *optarg)
{
	set_bsearch_size = true;
	opt_bsearch_size = get_uint64_byte(optarg);
	check_range("bsearch-size", opt_bsearch_size,
		MIN_BSEARCH_SIZE, MAX_BSEARCH_SIZE);
}

/*
 *  cmp()
 *	compare int32 values for bsearch
 */
static int cmp(const void *p1, const void *p2)
{
	const int32_t *i1 = (const int32_t *)p1;
	const int32_t *i2 = (const int32_t *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

/*
 *  Monotonically increasing values
 */
#define SETDATA(d, i, v, prev)		\
	d[i] = 1 + prev + (v & 0x7);	\
	v >>= 2;			\
	prev = d[i];			\
	i++;				\

/*
 *  stress_bsearch()
 *	stress bsearch
 */
int stress_bsearch(const args_t *args)
{
	int32_t *data, *ptr, prev = 0;
	size_t n, n8, i;

	if (!set_bsearch_size) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_bsearch_size = MAX_BSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_bsearch_size = MIN_BSEARCH_SIZE;
	}
	n = (size_t)opt_bsearch_size;
	n8 = (n + 7) & ~7;

	/* allocate in multiples of 8 */
	if ((data = calloc(n8, sizeof(int32_t))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	/* Populate with ascending data */
	prev = 0;
	for (i = 0; i < n;) {
		uint64_t v = mwc64();

		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
	}

	do {
		for (ptr = data, i = 0; i < n; i++, ptr++) {
			int32_t *result;
			result = bsearch(ptr, data, n, sizeof(*ptr), cmp);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu could "
						"not be found\n",
						args->name, i);
				else if (*result != *ptr)
					pr_fail("%s: element %zu "
						"found %" PRIu32
						", expecting %" PRIu32 "\n",
						args->name, i, *result, *ptr);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	free(data);
	return EXIT_SUCCESS;
}
