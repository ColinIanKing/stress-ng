/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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
	{ NULL,	"shellsort N",	   "start N workers shell sorting 32 bit random integers" },
	{ NULL,	"shellsort-ops N",  "stop after N shell sort bogo operations" },
	{ NULL,	"shellsort-size N", "number of 32 bit integers to sort" },
	{ NULL,	NULL,		   NULL }
};

/*
 *  stress_set_shellsort_size()
 *	set shellsort size
 */
static int stress_set_shellsort_size(const char *opt)
{
	uint64_t shellsort_size;

	shellsort_size = get_uint64(opt);
	check_range("shellsort-size", shellsort_size,
		MIN_SHELLSORT_SIZE, MAX_SHELLSORT_SIZE);
	return set_setting("shellsort-size", TYPE_ID_UINT64, &shellsort_size);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_shellsort_integers,	stress_set_shellsort_size },
	{ 0,				NULL }
};

/*
 *  stress_shellsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_shellsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_shellsort_cmp_1()
 *	shellsort comparison - sort on int32 values
 */
static int stress_shellsort_cmp_1(const void *p1, const void *p2)
{
	const int32_t *i1 = (const int32_t *)p1;
	const int32_t *i2 = (const int32_t *)p2;

	return *i1 > *i2;
}

/*
 *  stress_shellsort_cmp_2()
 *	shellsort comparison - reverse sort on int32 values
 */
static int stress_shellsort_cmp_2(const void *p1, const void *p2)
{
	const int32_t *i1 = (const int32_t *)p1;
	const int32_t *i2 = (const int32_t *)p2;

	return *i1 < *i2;
}

/*
 *  stress_shellsort_cmp_3()
 *	shellsort comparison - sort on int8 values
 */
static int stress_shellsort_cmp_3(const void *p1, const void *p2)
{
	const int8_t *i1 = (const int8_t *)p1;
	const int8_t *i2 = (const int8_t *)p2;

	/* Force re-ordering on 8 bit value */
	return *i1 > *i2;
}

static int shellsort32(void *base, size_t nmemb,
	int (*compar)(const void *, const void *))
{
	register size_t gap;
	uint32_t *array = (uint32_t *)base;

	for (gap = nmemb >> 1; gap > 0; gap >>= 1) {
		register size_t i;

		for (i = gap; i < nmemb; i++) {
			register size_t j;
			uint32_t temp = array[i];

			for (j = i; j >= gap && compar(&array[j - gap], &temp) > 0; j -= gap) {
				array[j] = array[j - gap];
			}
			array[j] = temp;
		}
	}
	return 0;
}

static int shellsort8(void *base, size_t nmemb,
	int (*compar)(const void *, const void *))
{
	register size_t gap;
	uint8_t *array = (uint8_t *)base;

	for (gap = nmemb >> 1; gap > 0; gap >>= 1) {
		register size_t i;

		for (i = gap; i < nmemb; i++) {
			register size_t j;
			uint8_t temp = array[i];

			for (j = i; j >= gap && compar(&array[j - gap], &temp) > 0; j -= gap) {
				array[j] = array[j - gap];
			}
			array[j] = temp;
		}
	}
	return 0;
}

static int shellsort(void *base, size_t nmemb, size_t size,
	int (*compar)(const void *, const void *))
{

	if (size == sizeof(uint32_t))
		return shellsort32(base, nmemb, compar);
	if (size == sizeof(uint8_t))
		return shellsort8(base, nmemb, compar);
	return -1;
}

/*
 *  stress_shellsort()
 *	stress shellsort
 */
static int stress_shellsort(const args_t *args)
{
	uint64_t shellsort_size = DEFAULT_SHELLSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	if (!get_setting("shellsort-size", &shellsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shellsort_size = MAX_SHELLSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shellsort_size = MIN_SHELLSORT_SIZE;
	}
	n = (size_t)shellsort_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_shellsort_handler, &old_action) < 0) {
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
	for (ptr = data, i = 0; i < n; i++) {
		*ptr++ = mwc32();
	}

	do {
		/* Sort "random" data */
		if (shellsort(data, n, sizeof(*data), stress_shellsort_cmp_1) < 0) {
			pr_fail("%s: shellsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
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
		}
		if (!g_keep_stressing_flag)
			break;

		/* Reverse sort */
		if (shellsort(data, n, sizeof(*data), stress_shellsort_cmp_2) < 0) {
			pr_fail("%s: reversed shellsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
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
		}
		if (!g_keep_stressing_flag)
			break;
		/* And re-order by byte compare */
		if (shellsort(data, n * 4, sizeof(uint8_t), stress_shellsort_cmp_3) < 0) {
			pr_fail("%s: shellsort failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		}

		/* Reverse sort this again */
		if (shellsort(data, n, sizeof(*data), stress_shellsort_cmp_2) < 0) {
			pr_fail("%s: reversed shellsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
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

stressor_info_t stress_shellsort_info = {
	.stressor = stress_shellsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
