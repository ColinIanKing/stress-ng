/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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

#define MIN_SHELLSORT_SIZE	(1 * KB)
#define MAX_SHELLSORT_SIZE	(4 * MB)
#define DEFAULT_SHELLSORT_SIZE	(256 * KB)

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

static const stress_help_t help[] = {
	{ NULL,	"shellsort N",	   "start N workers shell sorting 32 bit random integers" },
	{ NULL,	"shellsort-ops N",  "stop after N shell sort bogo operations" },
	{ NULL,	"shellsort-size N", "number of 32 bit integers to sort" },
	{ NULL,	NULL,		   NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_shellsort_size, "shellsort-size", TYPE_ID_UINT64, MIN_SHELLSORT_SIZE, MAX_SHELLSORT_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)
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
		stress_no_return();
	}
}
#endif

static inline void OPTIMIZE3 shellsort32(void *base, size_t nmemb,
	int (*compar)(const void *, const void *))
{
	register size_t gap;
	register uint32_t *const array = (uint32_t *)base;

	for (gap = nmemb >> 1; gap > 0; gap >>= 1) {
		register size_t i;

		for (i = gap; i < nmemb; i++) {
			register size_t j;
			const uint32_t temp = array[i];

			for (j = i; (j >= gap) && (compar(&array[j - gap], &temp) > 0); j -= gap) {
				array[j] = array[j - gap];
			}
			array[j] = temp;
		}
	}
}

/*
 *  stress_shellsort()
 *	stress shellsort
 */
static int OPTIMIZE3 stress_shellsort(stress_args_t *args)
{
	uint64_t shellsort_size = DEFAULT_SHELLSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, data_size;
	double rate;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	NOCLOBBER int rc = EXIT_SUCCESS;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	if (!stress_get_setting("shellsort-size", &shellsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			shellsort_size = MAX_SHELLSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			shellsort_size = MIN_SHELLSORT_SIZE;
	}
	n = (size_t)shellsort_size;
	data_size = n * sizeof(*data);

	data = (int32_t *)stress_mmap_populate(NULL, data_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu 32 bit integers%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, n, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_collapse(data, data_size);
	stress_set_vma_anon_name(data, data_size, "shellsort-data");

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_shellsort_handler, &old_action) < 0) {
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
		shellsort32(data, n, stress_sort_cmp_fwd_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (UNLIKELY(verify)) {
			register size_t i;

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
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		shellsort32(data, n, stress_sort_cmp_rev_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (UNLIKELY(verify)) {
			register size_t i;

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
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* And re-order */
		stress_sort_data_int32_mangle(data, n);
		stress_sort_compare_reset();

		/* Reverse sort this again */
		stress_sort_compare_reset();
		t = stress_time_now();
		shellsort32(data, n, stress_sort_cmp_rev_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (UNLIKELY(verify)) {
			register size_t i;

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
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "shellsort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "shellsort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f shellsort comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);

	return rc;
}

const stressor_info_t stress_shellsort_info = {
	.stressor = stress_shellsort,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
