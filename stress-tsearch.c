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
	{ NULL,	"tsearch N",		"start N workers that exercise a tree search" },
	{ NULL,	"tsearch-ops N",	"stop after N tree search bogo operations" },
	{ NULL,	"tsearch-size N",	"number of 32 bit integers to tsearch" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_tsearch_size()
 *      set tsearch size from given option string
 */
static int stress_set_tsearch_size(const char *opt)
{
	uint64_t tsearch_size;

	tsearch_size = get_uint64(opt);
	check_range("tsearch-size", tsearch_size,
		MIN_TSEARCH_SIZE, MAX_TSEARCH_SIZE);
	return set_setting("tsearch-size", TYPE_ID_UINT64, &tsearch_size);
}

/*
 *  cmp()
 *	sort on int32 values
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
 *  stress_tsearch()
 *	stress tsearch
 */
static int stress_tsearch(const args_t *args)
{
	uint64_t tsearch_size = DEFAULT_TSEARCH_SIZE;
	int32_t *data;
	size_t i, n;

	if (!get_setting("tsearch-size", &tsearch_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			tsearch_size = MAX_TSEARCH_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			tsearch_size = MIN_TSEARCH_SIZE;
	}
	n = (size_t)tsearch_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_fail_dbg("calloc");
		return EXIT_NO_RESOURCE;
	}

	do {
		void *root = NULL;

		/* Step #1, populate tree */
		for (i = 0; i < n; i++) {
			data[i] = ((mwc32() & 0xfff) << 20) ^ i;
			if (tsearch(&data[i], &root, cmp) == NULL) {
				size_t j;

				pr_err("%s: cannot allocate new "
					"tree node\n", args->name);
				for (j = 0; j < i; j++)
					tdelete(&data[j], &root, cmp);
				goto abort;
			}
		}
		/* Step #2, find */
		for (i = 0; g_keep_stressing_flag && i < n; i++) {
			void **result;

			result = tfind(&data[i], &root, cmp);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (result == NULL)
					pr_fail("%s: element %zu "
						"could not be found\n",
						args->name, i);
				else {
					int32_t *val;
					val = *result;
					if (*val != data[i])
						pr_fail("%s: element "
							"%zu found %" PRIu32
							", expecting %" PRIu32 "\n",
							args->name, i, *val, data[i]);
				}
			}
		}
		/* Step #3, delete */
		for (i = 0; i < n; i++) {
			void **result;

			result = tdelete(&data[i], &root, cmp);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) && (result == NULL))
				pr_fail("%s: element %zu could not "
					"be found\n", args->name, i);
		}
		inc_counter(args);
	} while (keep_stressing());

abort:
	free(data);
	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_tsearch_size,	stress_set_tsearch_size },
	{ 0,			NULL }
};

stressor_info_t stress_tsearch_info = {
	.stressor = stress_tsearch,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
