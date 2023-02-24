/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
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
#include "core-pragma.h"

/*
 *  stress_mmap_set()
 *	set mmap'd data, touching pages in
 *	a specific pattern - check with
 *	mmap_check().
 */
void stress_mmap_set(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += page_size) {
		if (!keep_stressing_flag())
			break;
PRAGMA_UNROLL_N(8)
		for (j = 0; j < page_size; j++)
			*ptr++ = val++;
		val++;
	}
}

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
int stress_mmap_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += page_size) {
		if (!keep_stressing_flag())
			break;
PRAGMA_UNROLL_N(8)
		for (j = 0; j < page_size; j++)
			if (*ptr++ != val++)
				return -1;
		val++;
	}
	return 0;
}
