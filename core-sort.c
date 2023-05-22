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

uint64_t stress_sort_compares ALIGN64;

void stress_sort_compare_reset(void)
{
	stress_sort_compares = 0;
}

uint64_t stress_sort_compare_get(void)
{
	return stress_sort_compares;
}

void OPTIMIZE3 stress_sort_data_int32_mangle(int32_t *data, const size_t n)
{
	const int32_t *end = data + n;

PRAGMA_UNROLL_N(8)
	while (data < end) {
		*(data++) ^= 0x80008000;
	}
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
