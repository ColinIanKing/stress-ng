/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#include "stress-ng.h"

static int stress_qsort_cmp_1(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	return *i1 - *i2;
}

static int stress_qsort_cmp_2(const void *p1, const void *p2)
{
	int32_t *i1 = (int32_t *)p1;
	int32_t *i2 = (int32_t *)p2;

	return *i2 - *i1;
}

static int stress_qsort_cmp_3(const void *p1, const void *p2)
{
	int8_t *i1 = (int8_t *)p1;
	int8_t *i2 = (int8_t *)p2;

	/* Force byte-wise re-ordering */
	return *i1 - (*i2 ^ *i1);
}

/*
 *  stress_qsort()
 *	stress qsort
 */
int stress_qsort(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int32_t *data, *ptr;
	const size_t n = (size_t)opt_qsort_size;
	size_t i;

	(void)instance;
	if ((data = malloc(sizeof(int32_t) * n)) == NULL) {
		pr_failed_dbg(name, "malloc");
		return EXIT_FAILURE;
	}

	/* This is expensive, do it once */
	for (ptr = data, i = 0; i < n; i++)
		*ptr++ = mwc();

	do {
		/* Sort "random" data */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_1);
		if (!opt_do_run)
			break;
		/* Reverse sort */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_2);
		if (!opt_do_run)
			break;
		/* Reverse this again */
		qsort(data, n, sizeof(uint32_t), stress_qsort_cmp_1);
		if (!opt_do_run)
			break;
		/* And re-order by byte compare */
		qsort(data, n * 4, sizeof(uint8_t), stress_qsort_cmp_3);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	free(data);

	return EXIT_SUCCESS;
}
