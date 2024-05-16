/*
 * Copyright (C) 2024      Colin Ian King.
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

#define MIN_INSERTIONSORT_SIZE		(1 * KB)
#define MAX_INSERTIONSORT_SIZE		(4 * MB)
#define DEFAULT_INSERTIONSORT_SIZE	(16384)

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

static const stress_help_t help[] = {
	{ NULL,	"insertionsort N",	"start N workers heap sorting 32 bit random integers" },
	{ NULL,	"insertionsort-ops N",	"stop after N heap sort bogo operations" },
	{ NULL,	"insertionsort-size N",	"number of 32 bit integers to sort" },
	{ NULL,	NULL,			NULL }
};

static uint64_t OPTIMIZE3 insertionsort_fwd(int32_t *base, size_t nmemb)
{
	register size_t i;
	register uint64_t compares = 0;

	for (i = 1; i < nmemb; i++) {
		register int32_t tmp = base[i];
		register size_t j = i;

		while ((j > 0) && (base[j - 1] > tmp)) {
			base[j] = base[j - 1];
			j--;
		}
		base[j] = tmp;
		compares += (i - j - 1);
	}
	return compares;
}

static uint64_t OPTIMIZE3 insertionsort_rev(int32_t *base, size_t nmemb)
{
	register size_t i;
	register uint64_t compares = 0;

	for (i = 1; i < nmemb; i++) {
		register int32_t tmp = base[i];
		register size_t j = i;

		while ((j > 0) && (base[j - 1] < tmp)) {
			base[j] = base[j - 1];
			j--;
		}
		base[j] = tmp;
		compares += (i - j - 1);
	}
	return compares;
}

/*
 *  stress_set_insertionsort_size()
 *	set insertionsort size
 */
static int stress_set_insertionsort_size(const char *opt)
{
	uint64_t insertionsort_size;

	insertionsort_size = stress_get_uint64(opt);
	stress_check_range("insertionsort-size", insertionsort_size,
		MIN_INSERTIONSORT_SIZE, MAX_INSERTIONSORT_SIZE);
	return stress_set_setting("insertionsort-size", TYPE_ID_UINT64, &insertionsort_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_insertionsort_size,	stress_set_insertionsort_size },
	{ 0,				NULL }
};

/*
 *  stress_insertionsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_insertionsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_insertionsort()
 *	stress insertionsort
 */
static int stress_insertionsort(stress_args_t *args)
{
	uint64_t insertionsort_size = DEFAULT_INSERTIONSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;
	double rate;
	NOCLOBBER int rc = EXIT_SUCCESS;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;

	if (!stress_get_setting("insertionsort-size", &insertionsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			insertionsort_size = MAX_INSERTIONSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			insertionsort_size = MIN_INSERTIONSORT_SIZE;
	}
	n = (size_t)insertionsort_size;

	if ((data = calloc(n, sizeof(*data))) == NULL) {
		pr_inf_skip("%s: failed to allocate %zu integers, skipping stressor\n",
			args->name, n);
		return EXIT_NO_RESOURCE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_insertionsort_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}

	stress_sort_data_int32_init(data, n);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;

		stress_sort_data_int32_shuffle(data, n);

		/* Sort "random" data */
		stress_sort_compare_reset();
		t = stress_time_now();
		count += (double)insertionsort_fwd(data, n);
		duration += stress_time_now() - t;
		sorted += (double)n;
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr > *(ptr + 1)) {
					pr_fail("%s: sort error "
						"detected, incorrect ordering "
						"found\n", args->name);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
		if (!stress_continue_flag())
			break;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		count += (double)insertionsort_rev(data, n);
		duration += stress_time_now() - t;
		sorted += (double)n;
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr + 1)) {
					pr_fail("%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", args->name);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
		if (!stress_continue_flag())
			break;
		/* And re-order  */
		stress_sort_data_int32_mangle(data, n);
		stress_sort_compare_reset();

		/* Reverse sort this again */
		stress_sort_compare_reset();
		t = stress_time_now();
		count += (double)insertionsort_rev(data, n);
		duration += stress_time_now() - t;
		sorted += (double)n;
		if (g_opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr + 1)) {
					pr_fail("%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", args->name);
					rc = EXIT_FAILURE;
					break;
				}
			}
		}
		if (!stress_continue_flag())
			break;

		stress_bogo_inc(args);
	} while (stress_continue(args));

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "insertionsort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "insertionsort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	free(data);

	return rc;
}

stressor_info_t stress_insertionsort_info = {
	.stressor = stress_insertionsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
