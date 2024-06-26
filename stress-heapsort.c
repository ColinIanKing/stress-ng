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

static inline OPTIMIZE3 void heapsort_swap(void *p1, void *p2, register size_t size)
{
	switch (size) {
	case 8: {
			register uint64_t tmp64;

			tmp64 = *(uint64_t *)p1;
			*(uint64_t *)p1 = *(uint64_t *)p2;
			*(uint64_t *)p2 = tmp64;
			return;
		}
	case 4: {
			register uint32_t tmp32;

			tmp32 = *(uint32_t *)p1;
			*(uint32_t *)p1 = *(uint32_t *)p2;
			*(uint32_t *)p2 = tmp32;
			return;
		}
	case 2: {
			register uint16_t tmp16;

			tmp16 = *(uint16_t *)p1;
			*(uint16_t *)p1 = *(uint16_t *)p2;
			*(uint16_t *)p2 = tmp16;
			return;
		}
	default: {
			register uint8_t *u8p1 = (uint8_t *)p1;
			register uint8_t *u8p2 = (uint8_t *)p2;

			do {
				register uint8_t tmp;

				tmp = *(u8p1);
				*(u8p1++) = *(u8p2);
				*(u8p2++) = tmp;
			} while (--size);
			return;
		}
	}
}

static inline void heapsort_copy(void *p1, void *p2, register size_t size)
{
	register uint8_t *u8p1, *u8p2;

	switch (size) {
	case 8:
		*(uint64_t *)p1 = *(uint64_t *)p2;
		return;
	case 4:
		*(uint32_t *)p1 = *(uint32_t *)p2;
		return;
	case 2:
		*(uint16_t *)p1 = *(uint16_t *)p2;
		return;
	default:
		u8p1 = (uint8_t *)p1;
		u8p2 = (uint8_t *)p2;

		do {
			*(u8p1++) = *(u8p2++);
		} while (--size);
		return;
	}
}

static int heapsort_nonlibc(
	void *base,
	size_t nmemb,
	size_t size,
	int (*compar)(const void *, const void *))
{
	register uint8_t *u8base;
	register size_t l = (nmemb / 2) + 1;

	if (UNLIKELY(nmemb <= 1))
		return 0;
	if (UNLIKELY(size < 1)) {
		errno = EINVAL;
		return -1;
	}

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
			heapsort_swap(p2, p1, size);
		}
	}
	/*
	 *  Phase #2, insert into heap
	 */
	while (nmemb > 1) {
		register uint8_t *ptr = u8base + (nmemb * size);
		register size_t i, j;
		uint8_t tmp[size] ALIGN64;

		heapsort_copy(tmp, ptr, size);
		heapsort_copy(ptr, u8base + size, size);
		nmemb--;

		for (i = 1; (j = i * 2) <= nmemb; i = j) {
			register uint8_t *p1 = u8base + (j * size), *p2;

			if ((j < nmemb) && (compar(p1, p1 + size) < 0)) {
				p1 += size;
				++j;
			}
			p2 = u8base + (i * size);
			heapsort_copy(p2, p1, size);
		}
		for (;;) {
			register uint8_t *p1, *p2;

			j = i;
			i = j / 2;
			p1 = u8base + (j * size);
			p2 = u8base + (i * size);
			if ((j == 1) || (compar(tmp, p2) < 0)) {
				heapsort_copy(p1, tmp, size);
				break;
			}
			(void)heapsort_copy(p1, p2, size);
		}
	}
	return 0;
}

static const stress_heapsort_method_t stress_heapsort_methods[] = {
#if defined(HAVE_LIB_BSD)
	{ "heapsort-libc",		heapsort },
#endif
	{ "heapsort-nonlibc",		heapsort_nonlibc },
};

static int stress_set_heapsort_method(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_heapsort_methods); i++) {
		if (strcmp(opt, stress_heapsort_methods[i].name) == 0) {
			stress_set_setting("heapsort-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "heapsort-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_heapsort_methods); i++) {
		(void)fprintf(stderr, " %s", stress_heapsort_methods[i].name);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

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
	{ OPT_heapsort_size,	stress_set_heapsort_size },
	{ OPT_heapsort_method,	stress_set_heapsort_method },
	{ 0,				NULL }
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
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

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

stressor_info_t stress_heapsort_info = {
	.stressor = stress_heapsort,
	.class = CLASS_CPU_CACHE | CLASS_CPU | CLASS_MEMORY | CLASS_SEARCH,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
