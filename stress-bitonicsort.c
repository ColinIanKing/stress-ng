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
#include "core-pragma.h"
#include "core-sort.h"

#define MIN_BITONICSORT_SIZE		(1 * KB)
#define MAX_BITONICSORT_SIZE		(4 * MB)
#define DEFAULT_BITONICSORT_SIZE	(256 * KB)

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif
static uint64_t bitonic_count;

static const stress_help_t help[] = {
	{ NULL,	"bitonicsort N",	"start N workers bitonic sorting 32 bit random integers" },
	{ NULL,	"bitonicsort-ops N",  	"stop after N bitonic sort bogo operations" },
	{ NULL,	"bitonicsort-size N", 	"number of 32 bit integers to sort" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_bitonicsort_size, "bitonicsort-size", TYPE_ID_UINT64, MIN_BITONICSORT_SIZE, MAX_BITONICSORT_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_bitonicsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_bitonicsort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
		stress_no_return();
	}
}
#endif

static inline void OPTIMIZE3 bitonicsort32_fwd(void *base, const size_t nmemb)
{
	register uint32_t *const array = (uint32_t *)base;
	size_t k;

	for (k = 2; k <= nmemb; k = k + k) {
		register size_t j;

		for (j = k >> 1; j > 0; j >>= 1) {
			register size_t i;

			for (i = 0; i < nmemb; i++) {
				register const size_t l = i ^ j;

				if (l > i) {
					register const uint32_t ai = array[i];
					register const uint32_t al = array[l];

					if ((((i & k) == 0) && (ai > al)) ||
					    (((i & k) != 0) && (ai < al))) {
						bitonic_count++;
						array[i] = al;
						array[l] = ai;
					}
				}
			}
		}
	}
}

static inline void OPTIMIZE3 bitonicsort32_rev(void *base, const size_t nmemb)
{
	register uint32_t *const array = (uint32_t *)base;
	size_t k;

	for (k = 2; k <= nmemb; k = k + k) {
		register size_t j;

		for (j = k >> 1; j > 0; j >>= 1) {
			register size_t i;

			for (i = 0; i < nmemb; i++) {
				register const size_t l = i ^ j;

				if (l < i) {
					register const uint32_t ai = array[i];
					register const uint32_t al = array[l];

					if ((((i & k) == 0) && (ai > al)) ||
					    (((i & k) != 0) && (ai < al))) {
						bitonic_count++;
						array[i] = al;
						array[l] = ai;
					}
				}
			}
		}
	}
}

/*
 *  stress_bitonicsort()
 *	stress bitonicsort
 */
static int OPTIMIZE3 stress_bitonicsort(stress_args_t *args)
{
	uint64_t bitonicsort_size = DEFAULT_BITONICSORT_SIZE;
	int32_t *data, *ptr;
	size_t n, data_size;
	NOCLOBBER int rc = EXIT_SUCCESS;
	double rate;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	if (!stress_get_setting("bitonicsort-size", &bitonicsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			bitonicsort_size = MAX_BITONICSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			bitonicsort_size = MIN_BITONICSORT_SIZE;
	}
	n = (size_t)bitonicsort_size;
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
	stress_set_vma_anon_name(data, data_size, "bitonicsort-data");

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_bitonicsort_handler, &old_action) < 0) {
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
		bitonic_count = 0;
		t = stress_time_now();
		bitonicsort32_fwd(data, n);
		duration += stress_time_now() - t;
		count += (double)bitonic_count;
		sorted += (double)n;

		if (UNLIKELY(verify)) {
			register size_t i;

PRAGMA_UNROLL_N(4)
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
		bitonic_count = 0;
		t = stress_time_now();
		bitonicsort32_rev(data, n);
		duration += stress_time_now() - t;
		count += (double)bitonic_count;
		sorted += (double)n;

		if (UNLIKELY(verify)) {
			register size_t i;

PRAGMA_UNROLL_N(4)
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

		/* Reverse sort this again */
		bitonic_count = 0;
		t = stress_time_now();
		bitonicsort32_rev(data, n);
		duration += stress_time_now() - t;
		count += (double)bitonic_count;
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

		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "bitonicsort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "bitonicsort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f bitonicsort comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);

	return rc;
}

const stressor_info_t stress_bitonicsort_info = {
	.stressor = stress_bitonicsort,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
