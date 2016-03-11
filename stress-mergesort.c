/*
 * Copyright (C) 2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_MERGESORT)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <setjmp.h>
#include <signal.h>
#include <bsd/stdlib.h>

static uint64_t opt_mergesort_size = DEFAULT_MERGESORT_SIZE;
static bool set_mergesort_size = false;
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;

/*
 *  stress_mergesort_handler()
 *	SIGALRM generic handler
 */
static void MLOCKED stress_mergesort_handler(int dummy)
{
	(void)dummy;

	if (do_jmp) {
		do_jmp = false;
		siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	}
}

/*
 *  stress_set_mergesort_size()
 *	set mergesort size
 */
void stress_set_mergesort_size(const void *optarg)
{
	set_mergesort_size = true;
	opt_mergesort_size = get_uint64_byte(optarg);
	check_range("mergesort-size", opt_mergesort_size,
		MIN_MERGESORT_SIZE, MAX_MERGESORT_SIZE);
}

/*
 *  stress_mergesort_cmp_1()
 *	mergesort comparison - sort on int32 values
 */
static int stress_mergesort_cmp_1(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

/*
 *  stress_mergesort_cmp_1()
 *	mergesort comparison - reverse sort on int32 values
 */
static int stress_mergesort_cmp_2(const void *p1, const void *p2)
{
	return stress_mergesort_cmp_1(p2, p1);
}

/*
 *  stress_mergesort_cmp_1()
 *	mergesort comparison - sort on int8 values
 */
static int stress_mergesort_cmp_3(const void *p1, const void *p2)
{
	int8_t *i1 = (int8_t *)p1;
	int8_t *i2 = (int8_t *)p2;

	/* Force re-ordering on 8 bit value */
	return *i1 - *i2;
}

/*
 *  stress_mergesort()
 *	stress mergesort
 */
int stress_mergesort(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int32_t *data, *ptr;
	size_t n, i;
	struct sigaction old_action;
	int ret;

	(void)instance;

	if (!set_mergesort_size) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_mergesort_size = MAX_MERGESORT_SIZE;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_mergesort_size = MIN_MERGESORT_SIZE;
	}
	n = (size_t)opt_mergesort_size;

	if ((data = calloc(n, sizeof(int32_t))) == NULL) {
		pr_fail_dbg(name, "malloc");
		return EXIT_FAILURE;
	}

	if (stress_sighandler(name, SIGALRM, stress_mergesort_handler, &old_action) < 0) {
		free(data);
		return EXIT_FAILURE;
	}

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		/*
		 * We return here if SIGALRM jmp'd back
		 */
		(void)stress_sigrestore(name, SIGALRM, &old_action);
		goto tidy;
	}

	/* This is expensive, do it once */
	for (ptr = data, i = 0; i < n; i++)
		*ptr++ = mwc32();

	do {
		/* Sort "random" data */
		mergesort(data, n, sizeof(uint32_t), stress_mergesort_cmp_1);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr > *(ptr+1)) {
					pr_fail(stderr, "%s: sort error "
						"detected, incorrect ordering "
						"found\n", name);
					break;
				}
			}
		}
		if (!opt_do_run)
			break;

		/* Reverse sort */
		mergesort(data, n, sizeof(uint32_t), stress_mergesort_cmp_2);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr+1)) {
					pr_fail(stderr, "%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", name);
					break;
				}
			}
		}
		if (!opt_do_run)
			break;
		/* And re-order by byte compare */
		mergesort(data, n * 4, sizeof(uint8_t), stress_mergesort_cmp_3);

		/* Reverse sort this again */
		mergesort(data, n, sizeof(uint32_t), stress_mergesort_cmp_2);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			for (ptr = data, i = 0; i < n - 1; i++, ptr++) {
				if (*ptr < *(ptr+1)) {
					pr_fail(stderr, "%s: reverse sort "
						"error detected, incorrect "
						"ordering found\n", name);
					break;
				}
			}
		}
		if (!opt_do_run)
			break;

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	do_jmp = false;
	(void)stress_sigrestore(name, SIGALRM, &old_action);
tidy:
	free(data);

	return EXIT_SUCCESS;
}

#endif
