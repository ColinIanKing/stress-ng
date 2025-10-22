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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-sort.h"
#include "core-target-clones.h"

#define MIN_MERGESORT_SIZE	(1 * KB)
#define MAX_MERGESORT_SIZE	(4 * MB)
#define DEFAULT_MERGESORT_SIZE	(256 * KB)

static const stress_help_t help[] = {
	{ NULL,	"mergesort N",		"start N workers merge sorting 32 bit random integers" },
	{ NULL,	"mergesort-method M",	"select sort method [ method-libc | method-nonlibc" },
	{ NULL,	"mergesort-ops N",	"stop after N merge sort bogo operations" },
	{ NULL,	"mergesort-size N",	"number of 32 bit integers to sort" },
	{ NULL,	NULL,			NULL }
};

#if !defined(__OpenBSD__) &&	\
    !defined(__NetBSD__) &&	\
    defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

typedef int (*mergesort_func_t)(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *));

typedef struct {
	const char *name;
	const mergesort_func_t mergesort_func;
} stress_mergesort_method_t;

#define IDX(base, idx, size)	((base) + ((idx) * (size)))

static inline void ALWAYS_INLINE mergesort_copy(uint8_t *RESTRICT p1, uint8_t *RESTRICT p2, register size_t size)
{
	register const uint32_t *u32end = (uint32_t *)(p1 + size);
	register uint32_t *u32p1 = (uint32_t *)p1;
	register uint32_t *u32p2 = (uint32_t *)p2;

	while (LIKELY(u32p1 < u32end))
		*(u32p1++) = *(u32p2++);
}

/*
 *  mergesort_partition4
 *  	partitioning with 4 byte data
 */
static inline void mergesort_partition4(
	register uint8_t * RESTRICT base,
	register uint8_t * RESTRICT lhs,
	const size_t left,
	const size_t right,
	int (*compar)(const void *, const void *))
{
	size_t mid, lhs_size, rhs_size, lhs_len, rhs_len;
	register ssize_t n;
	register uint8_t *rhs, *lhs_end, *rhs_end;

	mid = left + ((right - left) >> 1);
	if (left < mid)
		mergesort_partition4(base, lhs, left, mid, compar);
	if (mid + 1 < right)
		mergesort_partition4(base, lhs, mid + 1, right, compar);

	lhs_len = mid - left + 1;
	lhs_size = lhs_len * 4;
	rhs = lhs + lhs_size;
	rhs_len = right - mid;
	rhs_size = rhs_len * 4;

	mergesort_copy(lhs, IDX(base, left, 4), lhs_size);
	mergesort_copy(rhs, IDX(base, (mid + 1), 4), rhs_size);

	base = IDX(base, left, 4);
	lhs_end = rhs;
	rhs_end = rhs + rhs_size;

	while ((lhs < lhs_end) && (rhs < rhs_end)) {
		if (compar(lhs, rhs) < 0) {
			*(uint32_t *)base = *(uint32_t *)lhs;
			lhs += 4;
			base += 4;
		} else {
			*(uint32_t *)base = *(uint32_t *)rhs;
			rhs += 4;
			base += 4;
		}
	}

	n = lhs_end - lhs;
	if (n > 0) {
		mergesort_copy(base, lhs, n);
		base += n;
	}
	n = rhs_end - rhs;
	if (n > 0) {
		mergesort_copy(base, rhs, n);
	}
}

/*
 *  mergesort_partition
 *  	partitioning with size sized byte data
 */
static inline void mergesort_partition(
	register uint8_t * RESTRICT base,
	register uint8_t * RESTRICT lhs,
	const size_t left,
	const size_t right,
	const size_t size,
	int (*compar)(const void *, const void *))
{
	size_t mid, lhs_size, rhs_size, lhs_len, rhs_len;
	register ssize_t n;
	register uint8_t *rhs, *lhs_end, *rhs_end;

	mid = left + ((right - left) >> 1);
	if (left < mid)
		mergesort_partition(base, lhs, left, mid, size, compar);
	if (mid + 1 < right)
		mergesort_partition(base, lhs, mid + 1, right, size, compar);

	lhs_len = mid - left + 1;
	lhs_size = lhs_len * size;
	rhs = lhs + lhs_size;
	rhs_len = right - mid;
	rhs_size = rhs_len * size;

	mergesort_copy(lhs, IDX(base, left, size), lhs_size);
	mergesort_copy(rhs, IDX(base, (mid + 1), size), rhs_size);

	base = IDX(base, left, size);
	lhs_end = rhs;
	rhs_end = rhs + rhs_size;

	while ((lhs < lhs_end) && (rhs < rhs_end)) {
		if (compar(lhs, rhs) < 0) {
			shim_memcpy(base, lhs, size);
			lhs += size;
			if (lhs > lhs_end)
				break;
			base += size;
		} else {
			shim_memcpy(base, rhs, size);
			rhs += size;
			if (rhs > rhs_end)
				break;
			base += size;
		}
	}

	n = lhs_end - lhs;
	if (n > 0) {
		mergesort_copy(base, lhs, n);
		base += n;
	}
	n = rhs_end - rhs;
	if (n > 0)
		mergesort_copy(base, rhs, n);
}

static int mergesort_nonlibc(
	void *base,
	size_t nmemb,
	size_t size,
	int (*compar)(const void *, const void *))
{
	uint8_t *lhs;
	const size_t mmap_size = nmemb * size;

	lhs = (uint8_t *)stress_mmap_populate(NULL, mmap_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			-1, 0);
	if (lhs == MAP_FAILED)
		return -1;

	switch (size) {
	case 4:
		mergesort_partition4((uint8_t *)base, lhs, 0, nmemb - 1, compar);
		break;
	default:
		mergesort_partition((uint8_t *)base, lhs, 0, nmemb - 1, size, compar);
		break;
	}
	(void)munmap((void *)lhs, mmap_size);
	return 0;
}

static const stress_mergesort_method_t stress_mergesort_methods[] = {
#if defined(HAVE_LIB_BSD)
	{ "mergesort-libc",	mergesort },
#endif
	{ "mergesort-nonlibc",	mergesort_nonlibc },
};

static const char *stress_mergesort_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_mergesort_methods)) ? stress_mergesort_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_mergesort_size,   "mergesort-size",   TYPE_ID_UINT64, MIN_MERGESORT_SIZE, MAX_MERGESORT_SIZE, NULL },
	{ OPT_mergesort_method, "mergesort-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_mergesort_method },
	END_OPT,
};

#if !defined(__OpenBSD__) &&	\
    !defined(__NetBSD__) &&	\
    defined(HAVE_SIGLONGJMP)
/*
 *  stress_mergesort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED_TEXT stress_mergesort_handler(int signum)
{
	(void)signum;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}
#endif

/*
 *  stress_mergesort()
 *	stress mergesort
 */
static int stress_mergesort(stress_args_t *args)
{
	uint64_t mergesort_size = DEFAULT_MERGESORT_SIZE;
	int32_t *data, *ptr;
	size_t n, i, mergesort_method = 0, data_size;
	NOCLOBBER int rc = EXIT_SUCCESS;
	double rate;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	mergesort_func_t mergesort_func;
#if !defined(__OpenBSD__) &&	\
    !defined(__NetBSD__) &&	\
    defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	(void)stress_get_setting("mergesort-method", &mergesort_method);

	mergesort_func = stress_mergesort_methods[mergesort_method].mergesort_func;
	if (stress_instance_zero(args))
		pr_inf("%s: using method '%s'\n",
			args->name, stress_mergesort_methods[mergesort_method].name);

	if (!stress_get_setting("mergesort-size", &mergesort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mergesort_size = MAX_MERGESORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mergesort_size = MIN_MERGESORT_SIZE;
	}
	n = (size_t)mergesort_size;
	data_size = n * sizeof(*data);

	data = (int32_t *)stress_mmap_populate(NULL, data_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE,
			-1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed allocating %zu integers%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, n, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_collapse(data, data_size);
	stress_set_vma_anon_name(data, data_size, "mergesort-data");

#if !defined(__OpenBSD__) &&	\
    !defined(__NetBSD__) &&	\
    defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_mergesort_handler, &old_action) < 0) {
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

		stress_sort_compare_reset();
		t = stress_time_now();
		/* Sort "random" data */
		if (UNLIKELY(mergesort_func(data, n, sizeof(*data), stress_sort_cmp_fwd_int32) < 0)) {
			pr_fail("%s: mergesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
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
		if (mergesort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed mergesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
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

		/* And re-order */
		stress_sort_data_int32_mangle(data, n);
		stress_sort_compare_reset();

		/* Reverse sort this again */
		stress_sort_compare_reset();
		t = stress_time_now();
		if (mergesort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32) < 0) {
			pr_fail("%s: reversed mergesort of random data failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
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
		stress_bogo_inc(args);
	} while (stress_continue(args));

#if !defined(__OpenBSD__) &&	\
    !defined(__NetBSD__) &&	\
    defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "mergesort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "mergesort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f mergesort comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);

	return rc;
}

const stressor_info_t stress_mergesort_info = {
	.stressor = stress_mergesort,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
