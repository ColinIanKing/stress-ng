/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-sort.h"
#include "core-pragma.h"

#define MIN_HEAPSORT_SIZE	(1 * KB)
#define MAX_HEAPSORT_SIZE	(4 * MB)
#define DEFAULT_HEAPSORT_SIZE	(16384)

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

static const stress_help_t help[] = {
	{ NULL,	"bubblesort N",	   	"start N workers heap sorting 32 bit random integers" },
	{ NULL, "bubblesort-method M",	"select sort method [ bubblesort-libc | bubblesort-nonlibc" },
	{ NULL,	"bubblesort-ops N",	"stop after N heap sort bogo operations" },
	{ NULL,	"bubblesort-size N",	"number of 32 bit integers to sort" },
	{ NULL,	NULL,		   NULL }
};

typedef int (*bubblesort_func_t)(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

typedef struct {
	const char *name;
	const bubblesort_func_t bubblesort_func;
} stress_bubblesort_method_t;

static int bubblesort_fast(
	void *base,
	size_t nmemb,
	size_t size,
	int (*compar)(const void *, const void *))
{
	sort_swap_func_t swap_func;

	if (UNLIKELY(nmemb <= 1))
		return 0;
	if (UNLIKELY(size < 1)) {
		errno = EINVAL;
		return -1;
	}

	swap_func = sort_swap_func(size);

	do {
		register size_t i, n = 0;
		register uintptr_t p1 = (uintptr_t)base;
		register uintptr_t p2 = size + (uintptr_t)base;

PRAGMA_UNROLL_N(4)
		for (i = 1; i < nmemb; i++) {
			if (compar((void *)p1, (void *)p2) > 0) {
				swap_func((void *)p1, (void *)p2, size);
				n = i;
			}
			p1 = p2;
			p2 += size;
		}
		nmemb = n;
	} while (LIKELY(nmemb > 1));

	return 0;
}

static int bubblesort_naive(
	void *base,
	size_t nmemb,
	size_t size,
	int (*compar)(const void *, const void *))
{
	bool swapped;
	sort_swap_func_t swap_func;

	if (UNLIKELY(nmemb <= 1))
		return 0;
	if (UNLIKELY(size < 1)) {
		errno = EINVAL;
		return -1;
	}

	swap_func = sort_swap_func(size);

	do {
		register size_t i;
		register uintptr_t p1 = (uintptr_t)base;
		register uintptr_t p2 = size + (uintptr_t)base;

		swapped = false;
PRAGMA_UNROLL_N(4)
		for (i = 1; i < nmemb; i++) {
			if (compar((void *)p1, (void *)p2) > 0) {
				swap_func((void *)p1, (void *)p2, size);
				swapped = true;
			}
			p1 = p2;
			p2 += size;
		}
		nmemb--;
	} while (LIKELY(swapped));

	return 0;
}

static const stress_bubblesort_method_t stress_bubblesort_methods[] = {
	{ "bubblesort-fast",	bubblesort_fast },
	{ "bubblesort-naive",	bubblesort_naive },
};

static const char *stress_bubblesort_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_bubblesort_methods)) ? stress_bubblesort_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_bubblesort_size,   "bubblesort-size",   TYPE_ID_UINT64, MIN_HEAPSORT_SIZE, MAX_HEAPSORT_SIZE, NULL },
	{ OPT_bubblesort_method, "bubblesort-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_bubblesort_method },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_bubblesort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_bubblesort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
		stress_no_return();
	}
}
#endif

/*
 *  stress_bubblesort()
 *	stress bubblesort
 */
static int stress_bubblesort(stress_args_t *args)
{
	uint64_t bubblesort_size = DEFAULT_HEAPSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i, data_size, bubblesort_method = 0;
	double rate;
	NOCLOBBER int rc = EXIT_SUCCESS;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	bubblesort_func_t bubblesort_func;
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	(void)stress_get_setting("bubblesort-method", &bubblesort_method);

	bubblesort_func = stress_bubblesort_methods[bubblesort_method].bubblesort_func;
	if (stress_instance_zero(args))
		pr_inf("%s: using method '%s'\n",
			args->name, stress_bubblesort_methods[bubblesort_method].name);

	if (!stress_get_setting("bubblesort-size", &bubblesort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bubblesort_size = MAX_HEAPSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bubblesort_size = MIN_HEAPSORT_SIZE;
	}
	n = (size_t)bubblesort_size;
	data_size = n * sizeof(*data);

	data = (int32_t *)stress_mmap_populate(NULL, data_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed allocating %zu 32 bit integers%s, "
				"skipping stressor\n", args->name, n,
				stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_collapse(data, data_size);
	stress_set_vma_anon_name(data, data_size, "bubblesort-data");

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_bubblesort_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}
#endif

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
		if (bubblesort_func(data, n, sizeof(*data), stress_sort_cmp_fwd_int32) < 0) {
			pr_fail("%s: bubblesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (UNLIKELY(*ptr > *(ptr + 1))) {
						pr_fail("%s: sort error "
							"detected, incorrect ordering "
							"found\n", args->name);
						rc = EXIT_FAILURE;
						break;
					}
				}
			}
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		if (bubblesort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed bubblesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (UNLIKELY(*ptr < *(ptr + 1))) {
						pr_fail("%s: reverse sort "
							"error detected, incorrect "
							"ordering found\n", args->name);
						rc = EXIT_FAILURE;
						break;
					}
				}
			}
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
		/* And re-order  */
		stress_sort_data_int32_mangle(data, n);
		stress_sort_compare_reset();

		/* Reverse sort this again */
		stress_sort_compare_reset();
		t = stress_time_now();
		if (bubblesort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed bubblesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		} else {
			duration += stress_time_now() - t;
			count += (double)stress_sort_compare_get();
			sorted += (double)n;
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
					if (UNLIKELY(*ptr < *(ptr + 1))) {
						pr_fail("%s: reverse sort "
							"error detected, incorrect "
							"ordering found\n", args->name);
						rc = EXIT_FAILURE;
						break;
					}
				}
			}
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;

		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "bubblesort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "bubblesort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f bubblesort comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);

	return rc;
}

const stressor_info_t stress_bubblesort_info = {
	.stressor = stress_bubblesort,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
