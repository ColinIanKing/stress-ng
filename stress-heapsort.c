/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

#if defined(HAVE_LIB_BSD)
#include <bsd/stdlib.h>

static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

static uint64_t opt_heapsort_size = DEFAULT_HEAPSORT_SIZE;
static bool set_heapsort_size = false;

/*
 *  stress_set_heapsort_size()
 *	set heapsort size
 */
void stress_set_heapsort_size(const void *optarg)
{
	set_heapsort_size = true;
	opt_heapsort_size = get_uint64_byte(optarg);
	check_range("heapsort-size", opt_heapsort_size,
		MIN_HEAPSORT_SIZE, MAX_HEAPSORT_SIZE);
}

#if defined(HAVE_LIB_BSD)

/*
 *  stress_heapsort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED stress_heapsort_handler(int dummy)
{
	(void)dummy;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_heapsort_cmp_1()
 *	heapsort comparison - sort on int32 values
 */
static int stress_heapsort_cmp_1(const void *p1, const void *p2)
{
	const int32_t *i1 = (const int32_t *)p1;
	const int32_t *i2 = (const int32_t *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

/*
 *  stress_heapsort_cmp_1()
 *	heapsort comparison - reverse sort on int32 values
 */
static int stress_heapsort_cmp_2(const void *p1, const void *p2)
{
	return stress_heapsort_cmp_1(p2, p1);
}

/*
 *  stress_heapsort_cmp_1()
 *	heapsort comparison - sort on int8 values
 */
static int stress_heapsort_cmp_3(const void *p1, const void *p2)
{
	const int8_t *i1 = (const int8_t *)p1;
	const int8_t *i2 = (const int8_t *)p2;

	/* Force re-ordering on 8 bit value */
	return *i1 - *i2;
}

/*
 *  stress_heapsort()
 *	stress heapsort
 */
int stress_heapsort(const args_t *args)
{
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	if (!set_heapsort_size) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_heapsort_size = MAX_HEAPSORT_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_heapsort_size = MIN_HEAPSORT_SIZE;
	}
	n = (size_t)opt_heapsort_size;

	if ((data = calloc(n, sizeof(int32_t))) == NULL) {
		pr_fail_dbg("malloc");
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_heapsort_handler, &old_action) < 0) {
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
	for (ptr = data, i = 0; i < n; i++)
		*ptr++ = mwc32();

	do {
		/* Sort "random" data */
		heapsort(data, n, sizeof(uint32_t), stress_heapsort_cmp_1);
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
		if (!g_keep_stressing_flag)
			break;

		/* Reverse sort */
		heapsort(data, n, sizeof(uint32_t), stress_heapsort_cmp_2);
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
		if (!g_keep_stressing_flag)
			break;
		/* And re-order by byte compare */
		heapsort(data, n * 4, sizeof(uint8_t), stress_heapsort_cmp_3);

		/* Reverse sort this again */
		heapsort(data, n, sizeof(uint32_t), stress_heapsort_cmp_2);
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
		if (!g_keep_stressing_flag)
			break;

		inc_counter(args);
	} while (keep_stressing());

	do_jmp = false;
	(void)stress_sigrestore(args->name, SIGALRM, &old_action);
tidy:
	free(data);

	return EXIT_SUCCESS;
}
#else
int stress_heapsort(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
