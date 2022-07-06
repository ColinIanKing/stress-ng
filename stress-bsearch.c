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

#if defined(HAVE_SEARCH_H)
#include <search.h>
#endif

#define MIN_BSEARCH_SIZE	(1 * KB)
#define MAX_BSEARCH_SIZE	(4 * MB)
#define DEFAULT_BSEARCH_SIZE	(64 * KB)

static const stress_help_t help[] = {
	{ NULL,	"bsearch N",	  "start N workers that exercise a binary search" },
	{ NULL,	"bsearch-ops N",  "stop after N binary search bogo operations" },
	{ NULL,	"bsearch-size N", "number of 32 bit integers to bsearch" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  stress_set_bsearch_size()
 *	set bsearch size from given option string
 */
static int stress_set_bsearch_size(const char *opt)
{
	uint64_t bsearch_size;

	bsearch_size = stress_get_uint64(opt);
	stress_check_range("bsearch-size", bsearch_size,
		MIN_BSEARCH_SIZE, MAX_BSEARCH_SIZE);
	return stress_set_setting("bsearch-size", TYPE_ID_UINT64, &bsearch_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_bsearch_size,	stress_set_bsearch_size },
	{ 0,			NULL },
};

#if defined(HAVE_BSEARCH)

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
do {					\
	d[i] = 1 + prev + (v & 0x7);	\
	v >>= 2;			\
	prev = d[i];			\
	i++;				\
} while (0)

/*
 *  stress_bsearch()
 *	stress bsearch
 */
static int stress_bsearch(const stress_args_t *args)
{
	int32_t *data, *ptr, prev = 0;
	size_t n, n8, i;
	uint64_t bsearch_size = DEFAULT_BSEARCH_SIZE;

	if (!stress_get_setting("bsearch-size", &bsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bsearch_size = MAX_BSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bsearch_size = MIN_BSEARCH_SIZE;
	}
	n = (size_t)bsearch_size;
	n8 = (n + 7) & ~7UL;

	/* allocate in multiples of 8 */
	if ((data = calloc(n8, sizeof(*data))) == NULL) {
		pr_dbg("%s: malloc failed, out of memory\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Populate with ascending data */
	prev = 0;
	for (i = 0; i < n;) {
		int32_t v = (int32_t)stress_mwc32();

		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
		SETDATA(data, i, v, prev);
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (ptr = data, i = 0; i < n; i++, ptr++) {
			int32_t *result;

			result = bsearch(ptr, data, n, sizeof(*ptr), cmp);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu could not be found\n",
						args->name, i);
				else if (*result != *ptr)
					pr_fail("%s: element %zu "
						"found %" PRIu32
						", expecting %" PRIu32 "\n",
						args->name, i, *result, *ptr);
			}
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(data);
	return EXIT_SUCCESS;
}

stressor_info_t stress_bsearch_info = {
	.stressor = stress_bsearch,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

stressor_info_t stress_bsearch_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#endif
