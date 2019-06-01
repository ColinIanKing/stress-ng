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

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

static const help_t help[] = {
        { "Q N", "qsort N",	"start N workers qsorting 32 bit random integers" },
        { NULL,	"qsort-ops N",	"stop after N qsort bogo operations" },
        { NULL,	"qsort-size N",	"number of 32 bit integers to sort" },
        { NULL,	NULL,		NULL }
};

/*
 *  stress_qsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_qsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_set_qsort_size()
 *	set qsort size
 */
static int stress_set_qsort_size(const char *opt)
{
	uint64_t qsort_size;

	qsort_size = get_uint64(opt);
	check_range("qsort-size", qsort_size,
		MIN_QSORT_SIZE, MAX_QSORT_SIZE);
	return set_setting("qsort-size", TYPE_ID_UINT64, &qsort_size);
}

/*
 *  stress_qsort_cmp_1()
 *	qsort comparison - sort on int32 values
 */
static int stress_qsort_cmp_1(const void *p1, const void *p2)
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
 *  stress_qsort_cmp_2()
 *	qsort comparison - reverse sort on int32 values
 */
static int stress_qsort_cmp_2(const void *p1, const void *p2)
{
	return stress_qsort_cmp_1(p2, p1);
}

/*
 *  stress_qsort_cmp_3()
 *	qsort comparison - sort on int8 values
 */
static int stress_qsort_cmp_3(const void *p1, const void *p2)
{
	const int8_t *i1 = (const int8_t *)p1;
	const int8_t *i2 = (const int8_t *)p2;

	/* Force re-ordering on 8 bit value */
	return *i1 - *i2;
}

/*
 *  stress_qsort()
 *	stress qsort
 */
static int stress_qsort(const args_t *args)
{
	uint64_t qsort_size = DEFAULT_QSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	if (!get_setting("qsort-size", &qsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			qsort_size = MAX_QSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			qsort_size = MIN_QSORT_SIZE;
	}
	n = (size_t)qsort_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_fail_dbg("calloc");
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_qsort_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	/* This is expensive, do it once */
	for (ptr = data, i = 0; i < n; i++)
		*ptr++ = mwc32();

	do {
		/* Sort "random" data */
		qsort(data, n, sizeof(*data), stress_qsort_cmp_1);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr > *(ptr+1)) {
					pr_fail("%s: sort error "
						"detected, incorrect ordering "
						"found\n", args->name);
					break;
				}
			}
		}
		if (!g_keep_stressing_flag)
			break;

		/* Reverse sort */
		qsort(data, n, sizeof(*data), stress_qsort_cmp_2);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr+1)) {
					pr_fail("%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", args->name);
					break;
				}
			}
		}
		if (!g_keep_stressing_flag)
			break;
		/* And re-order by byte compare */
		qsort((uint8_t *)data, n * 4, sizeof(uint8_t), stress_qsort_cmp_3);

		/* Reverse sort this again */
		qsort(data, n, sizeof(*data), stress_qsort_cmp_2);
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr+1)) {
					pr_fail("%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", args->name);
					break;
				}
			}
		}
		if (!g_keep_stressing_flag)
			break;

		inc_counter(args);
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	free(data);

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_qsort_integers,	stress_set_qsort_size },
	{ 0,			NULL }
};

stressor_info_t stress_qsort_info = {
	.stressor = stress_qsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
