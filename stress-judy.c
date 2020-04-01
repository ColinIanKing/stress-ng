/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"judy N",	"start N workers that exercise a judy array search" },
	{ NULL,	"judy-ops N",	"stop after N judy array search bogo operations" },
	{ NULL,	"judy-size N",	"number of 32 bit integers to insert into judy array" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_judy_size()
 *      set judy size from given option string
 */
static int stress_set_judy_size(const char *opt)
{
	uint64_t judy_size;

	judy_size = stress_get_uint64(opt);
	stress_check_range("judy-size", judy_size,
		MIN_JUDY_SIZE, MAX_JUDY_SIZE);
	return stress_set_setting("judy-size", TYPE_ID_UINT64, &judy_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_judy_size,	stress_set_judy_size },
	{ 0,			NULL }
};

#if defined(HAVE_JUDY_H) && \
    defined(HAVE_LIB_JUDY)
/*
 *  generate a unique large index position into a Judy array
 *  from a known small index
 */
static inline Word_t gen_index(const Word_t index)
{
	return ((~index & 0xff) << 24) | (index & 0x00ffffff);
}

/*
 *  stress_judy()
 *	stress judy
 */
static int stress_judy(const stress_args_t *args)
{
	uint64_t judy_size = DEFAULT_JUDY_SIZE;
	size_t n;
	Word_t i, j;

	if (!stress_get_setting("judy-size", &judy_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			judy_size = MAX_JUDY_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			judy_size = MIN_JUDY_SIZE;
	}
	n = (size_t)judy_size;

	do {
		Pvoid_t PJLArray = (Pvoid_t)NULL;
		Word_t *pvalue;
		int rc;

		/* Step #1, populate Judy array in sparse index order */
		for (i = 0; i < n; i++) {
			Word_t idx = gen_index(i);

			JLI(pvalue, PJLArray, idx);
			if ((pvalue == NULL) || (pvalue == PJERR)) {
				pr_err("%s: cannot allocate new "
					"judy node\n", args->name);
				for (j = 0; j < n; j++) {
					JLD(rc, PJLArray, idx);
				}
				goto abort;
			}
			*pvalue = i;
		}

		/* Step #2, find */
		for (i = 0; keep_stressing_flag() && i < n; i++) {
			Word_t idx = gen_index(i);

			JLG(pvalue, PJLArray, idx);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (!pvalue) {
					pr_fail("%s: element %" PRIu32
						"could not be found\n",
						args->name, (uint32_t)idx);
				} else {
					if ((uint32_t)*pvalue != i)
						pr_fail("%s: element "
							"%" PRIu32 " found %" PRIu32
							", expecting %" PRIu32 "\n",
							args->name, (uint32_t)idx,
							(uint32_t)*pvalue, (uint32_t)i);
				}
			}
		}

		/* Step #3, delete, reverse index order */
		for (j = n -1, i = 0; i < n; i++, j--) {
			Word_t idx = gen_index(j);

			JLD(rc, PJLArray, idx);
			if ((g_opt_flags & OPT_FLAGS_VERIFY) && (rc != 1))
				pr_fail("%s: element %" PRIu32 " could not "
					"be found\n", args->name, (uint32_t)idx);
		}
		inc_counter(args);
	} while (keep_stressing());

abort:
	return EXIT_SUCCESS;
}

stressor_info_t stress_judy_info = {
	.stressor = stress_judy,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_judy_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
