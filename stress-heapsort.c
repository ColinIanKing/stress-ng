/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

#define MIN_HEAPSORT_SIZE	(1 * KB)
#define MAX_HEAPSORT_SIZE	(4 * MB)
#define DEFAULT_HEAPSORT_SIZE	(256 * KB)

#if defined(HAVE_LIB_BSD)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

static const stress_help_t help[] = {
	{ NULL,	"heapsort N",	   "start N workers heap sorting 32 bit random integers" },
	{ NULL,	"heapsort-ops N",  "stop after N heap sort bogo operations" },
	{ NULL,	"heapsort-size N", "number of 32 bit integers to sort" },
	{ NULL,	NULL,		   NULL }
};

/*
 *  stress_set_heapsort_size()
 *	set heapsort size
 */
static int stress_set_heapsort_size(const char *opt)
{
	uint64_t heapsort_size;

	heapsort_size = stress_get_uint64(opt);
	stress_check_range("heapsort-size", heapsort_size,
		MIN_HEAPSORT_SIZE, MAX_HEAPSORT_SIZE);
	return stress_set_setting("heapsort-size", TYPE_ID_UINT64, &heapsort_size);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_heapsort_integers,	stress_set_heapsort_size },
	{ 0,				NULL }
};

#if defined(HAVE_LIB_BSD)

/*
 *  stress_heapsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_heapsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_heapsort()
 *	stress heapsort
 */
static int stress_heapsort(const stress_args_t *args)
{
	uint64_t heapsort_size = DEFAULT_HEAPSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;
	double rate;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;

	if (!stress_get_setting("heapsort-size", &heapsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			heapsort_size = MAX_HEAPSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			heapsort_size = MIN_HEAPSORT_SIZE;
	}
	n = (size_t)heapsort_size;

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

	if (stress_sighandler(args->name, SIGALRM, stress_heapsort_handler, &old_action) < 0) {
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
		if (heapsort(data, n, sizeof(*data), stress_sort_cmp_fwd_int32) < 0) {
			pr_fail("%s: heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (*ptr > *(ptr + 1)) {
						pr_fail("%s: sort error "
							"detected, incorrect ordering "
							"found\n", args->name);
						break;
					}
				}
			}
		}
		if (!stress_continue_flag())
			break;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		if (heapsort(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (*ptr < *(ptr + 1)) {
						pr_fail("%s: reverse sort "
							"error detected, incorrect "
							"ordering found\n", args->name);
						break;
					}
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
		if (heapsort(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (*ptr < *(ptr + 1)) {
						pr_fail("%s: reverse sort "
							"error detected, incorrect "
							"ordering found\n", args->name);
						break;
					}
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
	stress_metrics_set(args, 0, "heapsort comparisons per sec", rate);
	stress_metrics_set(args, 1, "heapsort comparisons per item", count / sorted);

	free(data);

	return EXIT_SUCCESS;
}

stressor_info_t stress_heapsort_info = {
	.stressor = stress_heapsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_heapsort_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without the BSD library"
};
#endif
