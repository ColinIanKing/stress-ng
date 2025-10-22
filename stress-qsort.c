/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-pragma.h"
#include "core-sort.h"
#include "core-target-clones.h"

#define THRESH 63

#define MIN_QSORT_SIZE		(1 * KB)
#define MAX_QSORT_SIZE		(4 * MB)
#define DEFAULT_QSORT_SIZE	(256 * KB)

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

typedef int (*comp_func_t)(const void *v1, const void *v2);
typedef void (*qsort_func_t)(void *base, size_t nmemb, size_t size, comp_func_t cmp);

static const stress_help_t help[] = {
	{ "Q N", "qsort N",		"start N workers qsorting 32 bit random integers" },
	{ NULL,	"qsort-method M",	"select qsort method [ qsort-libc | qsort_bm ]" },
	{ NULL,	"qsort-ops N",		"stop after N qsort bogo operations" },
	{ NULL,	"qsort-size N",		"number of 32 bit integers to sort" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	const char *name;
	const qsort_func_t qsort_func;
} stress_qsort_method_t;

#if defined(HAVE_SIGLONGJMP)
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
		stress_no_return();
	}
}
#endif

typedef uint32_t qsort_swap_type_t;

static inline size_t qsort_bm_minimum(const size_t x, const size_t y)
{
	return x <= y ? x : y;
}

static inline uint8_t ALWAYS_INLINE *qsort_bm_med3(uint8_t *a, uint8_t *b, uint8_t *c, comp_func_t cmp)
{
	return (cmp(a, b) < 0) ?
		((cmp(b, c) < 0) ? b : (cmp(a, c) < 0) ? c : a) :
		((cmp(b, c) > 0) ? b : (cmp(a, c) > 0) ? c : a);
}

static inline void ALWAYS_INLINE qsort_bm_swapfunc(uint8_t *a, uint8_t *b, size_t n, int swaptype)
{
	if (swaptype <= 1) {
		register qsort_swap_type_t * RESTRICT pi = (qsort_swap_type_t *)a;
		register qsort_swap_type_t * RESTRICT pj = (qsort_swap_type_t *)b;

PRAGMA_UNROLL_N(4)
		do {
			register qsort_swap_type_t tmp;

			tmp = *pi;
			*pi++ = *pj;
			*pj++ = tmp;
		} while ((n -= sizeof(qsort_swap_type_t)) > 0);
	} else {
		register uint8_t * RESTRICT pi = (uint8_t *)a;
		register uint8_t * RESTRICT pj = (uint8_t *)b;

PRAGMA_UNROLL_N(4)
		do {
			register uint8_t tmp;

			tmp = *pi;
			*pi++ = *pj;
			*pj++ = tmp;
		} while ((n -= sizeof(uint8_t)) > 0);
	}
}

static inline void ALWAYS_INLINE qsort_bm_swap(uint8_t *a, uint8_t *b, const size_t es, const int swaptype)
{
	if (swaptype == 0) {
		register qsort_swap_type_t tmp;

		tmp = *(qsort_swap_type_t *)a;
		*(qsort_swap_type_t *)a = *(qsort_swap_type_t *)b;
		*(qsort_swap_type_t *)b = tmp;
	} else {
		qsort_bm_swapfunc(a, b, es, swaptype);
	}
}


/*
 *  Bentley and MacIlroy’s quicksort, v2
 *  https://web.ecs.syr.edu/~royer/cis675/slides/07engSort.pdf
 */
static void TARGET_CLONES OPTIMIZE3 qsort_bm(void *base, size_t n, size_t es, comp_func_t cmp)
{
	uint8_t *a = (uint8_t *)base;
	const int swaptype = (((uintptr_t)a | (uintptr_t)es) % sizeof(qsort_swap_type_t)) ?
		2 : (es > sizeof(qsort_swap_type_t));
	uint8_t *pa, *pb, *pc, *pd, *pm, *pn, *pv;
	size_t s;
	qsort_swap_type_t v;

	if (n < THRESH) {
		for (pm = a + es; pm < a + (n * es); pm += es) {
			register uint8_t *p;

			for (p = pm; (p > a) && (cmp(p - es, p) > 0); p -= es) {
				qsort_bm_swap(p, p - es, es, swaptype);
			}
		}
		return;
	}
	pm = a + (n >> 1) * es;
	if (n > THRESH) {
		register uint8_t *p = a;

		pn = a + (n - 1) * es;
		if (n > 63) {
			s = (n >> 3) * es;
			p = qsort_bm_med3(p, p + s, p + (s << 1), cmp);
			pm = qsort_bm_med3(pm - s, pm, pm + s, cmp);
			pn = qsort_bm_med3(pn - (s << 1), pn - s, pn, cmp);
		}
		pm = qsort_bm_med3(p, pm, pn, cmp);
	}

	if (swaptype != 0) {
		pv = a;
		qsort_bm_swap(pv, pm, es, swaptype);
	} else {
		pv = (uint8_t *)&v;
		*(qsort_swap_type_t *)pv = *(qsort_swap_type_t *)pm;
	}

	pa = pb = a;
	pc = pd = a + (n - 1) * es;
	for (;;) {
		int r;

		while ((pb <= pc) && (r = cmp(pb, pv)) <= 0) {
			if (r == 0) {
				qsort_bm_swap(pa, pb, es, swaptype);
				pa += es;
			}
			pb += es;
		}
		while ((pb <= pc) && (r = cmp(pc, pv)) >= 0) {
			if (r == 0) {
				qsort_bm_swap(pc, pd, es, swaptype);
				pd -= es;
			}
			pc -= es;
		}
		if (pb > pc)
			break;
		qsort_bm_swap(pb, pc, es, swaptype);
		pb += es;
		pc -= es;
	}
	pn = a + (n * es);
	s = qsort_bm_minimum(pa - a, pb - pa);
	if (s > 0)
		qsort_bm_swapfunc(a, pb - s, s, swaptype);
	s = qsort_bm_minimum(pd - pc, pn - pd - es);
	if (s > 0)
		qsort_bm_swapfunc(pb, pn-s, s, swaptype);
	s = pb - pa;
	if (s > es)
		qsort_bm(a, s / es, es, cmp);
	s = pd - pc;
	if (s > es)
		qsort_bm(pn - s, s / es, es, cmp);
}

static const stress_qsort_method_t stress_qsort_methods[] = {
	{ "qsort-libc",		qsort },
	{ "qsort-bm",		qsort_bm },
};

static inline bool OPTIMIZE3 stress_qsort_verify_forward(
	stress_args_t *args,
	const int32_t *data,
	const size_t n,
	int *rc)
{
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register const int32_t *ptr = data;
		register const int32_t *end = data + n - 1;
		register int32_t val = *ptr;

PRAGMA_UNROLL_N(8)
		while (ptr < end) {
			register const int32_t next_val = *(ptr + 1);

			if (UNLIKELY(val > next_val))
				goto fail;

			ptr++;
			val = next_val;
		}
	}
	return true;

fail:
	pr_fail("%s: forward sort error detected, incorrect ordering found\n",
		args->name);
	*rc = EXIT_FAILURE;
	return false;
}

static inline bool OPTIMIZE3 stress_qsort_verify_reverse(
	stress_args_t *args,
	const int32_t *data,
	const size_t n,
	int *rc)
{
	if (g_opt_flags & OPT_FLAGS_VERIFY) {
		register const int32_t *ptr = data;
		register const int32_t *end = data + n - 1;
		register int32_t val = *ptr;

PRAGMA_UNROLL_N(8)
		while (ptr < end) {
			register int32_t next_val = *(ptr + 1);

			if (UNLIKELY(val < next_val))
				goto fail;

			ptr++;
			val = next_val;
		}
	}
	return true;

fail:
	pr_fail("%s: reverse sort error detected, incorrect ordering found\n",
		args->name);
	*rc = EXIT_FAILURE;
	return false;
}

/*
 *  stress_qsort()
 *	stress qsort
 */
static int OPTIMIZE3 stress_qsort(stress_args_t *args)
{
	uint64_t qsort_size = DEFAULT_QSORT_SIZE;
	int32_t *data;
	size_t n, data_size, qsort_method = 0;
	double rate;
	NOCLOBBER double duration = 0.0, count = 0.0, sorted = 0.0;
	NOCLOBBER int rc = EXIT_SUCCESS;
	qsort_func_t qsort_func;
#if defined(HAVE_SIGLONGJMP)
	struct sigaction old_action;
	int ret;
#endif

	stress_catch_sigill();

	(void)stress_get_setting("qsort-method", &qsort_method);
	if (!stress_get_setting("qsort-size", &qsort_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			qsort_size = MAX_QSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			qsort_size = MIN_QSORT_SIZE;
	}
	n = (size_t)qsort_size;
	data_size = n * sizeof(*data);
	data = (int32_t *)stress_mmap_populate(NULL, data_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (data == MAP_FAILED) {
		pr_inf_skip("%s: mmap failed allocating %zu 32 bit integers%s, errno=%d (%s), "
			"skipping stressor\n", args->name, n,
			stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)stress_madvise_collapse(data, data_size);
	stress_set_vma_anon_name(data, data_size, "qsort-data");

#if defined(HAVE_SIGLONGJMP)
	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(args->name, SIGALRM, &old_action);
		goto tidy;
	}
	if (stress_sighandler(args->name, SIGALRM, stress_qsort_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}
#endif

	stress_sort_data_int32_init(data, n);

	qsort_func = stress_qsort_methods[qsort_method].qsort_func;
	if (stress_instance_zero(args))
		pr_inf("%s: using method '%s'\n",
			args->name, stress_qsort_methods[qsort_method].name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;

		stress_sort_data_int32_shuffle(data, n);

		stress_sort_compare_reset();
		t = stress_time_now();
		/* Sort "random" data */
		qsort_func(data, n, sizeof(*data), stress_sort_cmp_fwd_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (!stress_qsort_verify_forward(args, data, n, &rc))
			break;

		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		qsort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (!stress_qsort_verify_reverse(args, data, n, &rc))
			break;

		if (UNLIKELY(!stress_continue_flag()))
			break;

		stress_sort_data_int32_mangle(data, n);
		stress_sort_compare_reset();
		t = stress_time_now();
		qsort_func((uint8_t *)data, n, sizeof(uint32_t), stress_sort_cmp_fwd_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		/* Reverse sort */
		stress_sort_compare_reset();
		t = stress_time_now();
		qsort_func(data, n, sizeof(*data), stress_sort_cmp_rev_int32);
		duration += stress_time_now() - t;
		count += (double)stress_sort_compare_get();
		sorted += (double)n;

		if (!stress_qsort_verify_reverse(args, data, n, &rc))
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
	stress_metrics_set(args, 0, "qsort comparisons per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "qsort comparisons per item",
		count / sorted, STRESS_METRIC_HARMONIC_MEAN);

	pr_dbg("%s: %.2f qsort comparisons per sec\n", args->name, rate);

	(void)munmap((void *)data, data_size);

	return rc;
}

static const char *stress_qsort_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_qsort_methods)) ? stress_qsort_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_qsort_size,   "qsort-size",   TYPE_ID_UINT64, MIN_QSORT_SIZE, MAX_QSORT_SIZE, NULL },
	{ OPT_qsort_method, "qsort-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_qsort_method },
	END_OPT,
};

const stressor_info_t stress_qsort_info = {
	.stressor = stress_qsort,
	.classifier = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SORT,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
