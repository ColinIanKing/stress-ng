/*
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-pragma.h"

static uint64_t stress_sort_compares;

void stress_sort_compare_reset(void)
{
	stress_sort_compares = 0;
}

uint64_t stress_sort_compare_get(void)
{
	return stress_sort_compares;
}

/*
 *  Monotonically increasing values
 */
#define SORT_SETDATA(d, i, v, prev)	\
do {					\
	d[i] = 1 + prev + (v & 0x7);	\
	v >>= 2;			\
	prev = d[i];			\
	i++;				\
} while (0)

void stress_sort_data_int32_init(int32_t *data, const size_t n)
{
	register int32_t prev = 0;
	register size_t i;

	for (i = 0; i < n;) {
		register int32_t v = (int32_t)stress_mwc32();

		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
		SORT_SETDATA(data, i, v, prev);
	}
}

void stress_sort_data_int32_shuffle(int32_t *data, const size_t n)
{
	register uint32_t const a = 16843009;
        register uint32_t const c = 826366247;
        register uint32_t seed = stress_mwc32();
	register uint32_t *data32 = (uint32_t *)data;
	register size_t i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < n; i++) {
		register uint32_t tmp;
		register uint32_t j = seed % n;

		seed = (a * seed + c);
		tmp = data32[i];
		data32[i] = data32[j];
		data32[j] = tmp;
	}
}

#define STRESS_SORT_CMP(name, type)				\
int OPTIMIZE3 stress_sort_cmp_ ## name(const void *p1, const void *p2)	\
{								\
	const type v1 = *(const type *)p1;			\
	const type v2 = *(const type *)p2;			\
								\
	stress_sort_compares++;					\
	if (v1 > v2)						\
		return 1;					\
	else if (v1 < v2)					\
		return -1;					\
	else							\
		return 0;					\
}

#define STRESS_SORT_CMP_REV(name, type)				\
int OPTIMIZE3 stress_sort_cmp_rev_ ## name(const void *p1, const void *p2)\
{								\
	const type v1 = *(const type *)p1;			\
	const type v2 = *(const type *)p2;			\
								\
	stress_sort_compares++;					\
	if (v1 < v2)						\
		return 1;					\
	else if (v1 > v2)					\
		return -1;					\
	else							\
		return 0;					\
}

STRESS_SORT_CMP(int8,  int8_t)
STRESS_SORT_CMP(int16, int16_t)
STRESS_SORT_CMP(int32, int32_t)
STRESS_SORT_CMP(int64, int64_t)

STRESS_SORT_CMP_REV(int8,  int8_t)
STRESS_SORT_CMP_REV(int16, int16_t)
STRESS_SORT_CMP_REV(int32, int32_t)
STRESS_SORT_CMP_REV(int64, int64_t)
