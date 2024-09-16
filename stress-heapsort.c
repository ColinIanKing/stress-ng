/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

static const stress_help_t help[] = {
	{ NULL,	"heapsort N",	   	"start N workers heap sorting 32 bit random integers" },
	{ NULL, "heapsort-method M",	"select sort method [ heapsort-libc | heapsort-nonlibc" },
	{ NULL,	"heapsort-ops N",	"stop after N heap sort bogo operations" },
	{ NULL,	"heapsort-size N",	"number of 32 bit integers to sort" },
	{ NULL,	NULL,		   NULL }
};

typedef int (*heapsort_func_t)(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

typedef struct {
	const char *name;
	const heapsort_func_t heapsort_func;
} stress_heapsort_method_t;

static int heapsort_nonlibc(
	void *base,
	size_t nmemb,
	size_t size,
	int (*compar)(const void *, const void *))
{
	register uint8_t *u8base;
	register size_t l = (nmemb / 2) + 1;
	sort_swap_func_t swap_func;
	sort_copy_func_t copy_func;

	if (UNLIKELY(nmemb <= 1))
		return 0;
	if (UNLIKELY(size < 1)) {
		errno = EINVAL;
		return -1;
	}

	swap_func = sort_swap_func(size);
	copy_func = sort_copy_func(size);

	/*
	 *  Phase #1, create initial heap
	 */
	u8base = (uint8_t *)base - size;
	while (--l) {
		register size_t i, j;

		for (i = l; (j = i * 2) <= nmemb; i = j) {
			register uint8_t *p1 = u8base + (j * size), *p2;

			if ((j < nmemb) && (compar(p1, p1 + size) < 0)) {
				p1 += size;
				++j;
			}
			p2 = u8base + (i * size);
			if (compar(p1, p2) <= 0)
				break;
			swap_func(p2, p1, size);
		}
	}
	/*
	 *  Phase #2, insert into heap
	 */
	while (nmemb > 1) {
		register uint8_t *ptr = u8base + (nmemb * size);
		register size_t i, j;
		uint8_t tmp[size] ALIGN64;

		copy_func(tmp, ptr, size);
		copy_func(ptr, u8base + size, size);
		nmemb--;

		for (i = 1; (j = i * 2) <= nmemb; i = j) {
			register uint8_t *p1 = u8base + (j * size), *p2;

			if ((j < nmemb) && (compar(p1, p1 + size) < 0)) {
				p1 += size;
				++j;
			}
			p2 = u8base + (i * size);
			copy_func(p2, p1, size);
		}
		for (;;) {
			register uint8_t *p1, *p2;

			j = i;
			i = j / 2;
			p1 = u8base + (j * size);
			p2 = u8base + (i * size);
			if ((j == 1) || (compar(tmp, p2) < 0)) {
				copy_func(p1, tmp, size);
				break;
			}
			(void)copy_func(p1, p2, size);
		}
	}
	return 0;
}

static const stress_heapsort_method_t stress_heapsort_methods[] = {
#if defined(HAVE_LIB_BSD)
	{ "heapsort-libc",	heapsort },
#endif
	{ "heapsort-nonlibc",	heapsort_nonlibc },
};

static const char *stress_heapsort_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_heapsort_methods)) ? stress_heapsort_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_heapsort_size,   "heapsort-size",   TYPE_ID_UINT64, MIN_HEAPSORT_SIZE, MAX_HEAPSORT_SIZE, NULL },
	{ OPT_heapsort_method, "heapsort-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_heapsort_method },
	END_OPT,
};

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
static int stress_heapsort(stress_args_t *args)
{
	uint64_t heapsort_size = DEFAULT_HEAPSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i, heapsort_method = 0;
	struct sigaction old_action;
	int ret;
	double rate;
	NOCLOBBER int rc = EXIT_SUCCESS;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	heapsort_func_t heapsort_func;

	(void)stress_get_setting("heapsort-method", &heapsort_method);

	heapsort_func = stress_heapsort_methods[heapsort_method].heapsort_func;
	if (args->instance == 0)
		pr_inf("%s: using method '%s'\n",
			args->name, stress_heapsort_methods[heapsort_method].name);

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

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;

		stress_sort_data_int32_shuffle(data, n);

		/* Sort "random" data */
		stress_sort_compare_reset();
		t = stress_time_now();
		if (heapsort_func(data, n, sizeof(*data), stress_sort_cmp_fwd_int32) < 0) {
			pr_fail("%s: heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
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
						rc = EXIT_FAILURE;
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
		if (heapsort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
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
						rc = EXIT_FAILURE;
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
		if (heapsort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed heapsort of random data failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
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
						rc = EXIT_FAILURE;
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
	stress_metrics_set(args, 0, "heapsort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "heapsort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	free(data);

	return rc;
}

const stressor_info_t stress_heapsort_info = {
	.stressor = stress_heapsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
