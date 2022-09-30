/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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

#define MIN_SHELLSORT_SIZE	(1 * KB)
#define MAX_SHELLSORT_SIZE	(4 * MB)
#define DEFAULT_SHELLSORT_SIZE	(256 * KB)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

static const stress_help_t help[] = {
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

	shellsort_size = stress_get_uint64(opt);
	stress_check_range("shellsort-size", shellsort_size,
		MIN_SHELLSORT_SIZE, MAX_SHELLSORT_SIZE);
	return stress_set_setting("shellsort-size", TYPE_ID_UINT64, &shellsort_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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

static int shellsort32(void *base, size_t nmemb,
	int (*compar)(const void *, const void *))
{
	register size_t gap;
	uint32_t *array = (uint32_t *)base;

	for (gap = nmemb >> 1; gap > 0; gap >>= 1) {
		register size_t i;

		for (i = gap; i < nmemb; i++) {
			register size_t j;
			const uint32_t temp = array[i];

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
			const uint8_t temp = array[i];

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
	else if (size == sizeof(uint8_t))
		return shellsort8(base, nmemb, compar);
	else return -1;
}

/*
 *  stress_shellsort()
 *	stress shellsort
 */
static int stress_shellsort(const stress_args_t *args)
{
	uint64_t shellsort_size = DEFAULT_SHELLSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	if (!stress_get_setting("shellsort-size", &shellsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shellsort_size = MAX_SHELLSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shellsort_size = MIN_SHELLSORT_SIZE;
	}
	n = (size_t)shellsort_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_err("%s: malloc failed, out of memory\n", args->name);
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
		*ptr++ = (int32_t)stress_mwc32();
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/* Sort "random" data */
		if (shellsort(data, n, sizeof(*data), stress_sort_cmp_int32) < 0) {
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
		if (!keep_stressing_flag())
			break;

		/* Reverse sort */
		if (shellsort(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
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
		if (!keep_stressing_flag())
			break;
		/* And re-order by byte compare */
		if (shellsort(data, n * 4, sizeof(uint8_t), stress_sort_cmp_int8) < 0) {
			pr_fail("%s: shellsort failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		}

		/* Reverse sort this again */
		if (shellsort(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
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
		if (!keep_stressing_flag())
			break;

		inc_counter(args);
	} while (keep_stressing(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(data);

	return EXIT_SUCCESS;
}

stressor_info_t stress_shellsort_info = {
	.stressor = stress_shellsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
