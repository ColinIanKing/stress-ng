/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#include <stdint.h>
#include <string.h>

#define VEC_ELEMENTS	(32)

int main(int argc, char **argv)
{
	uint64_t data __attribute__ ((vector_size(VEC_ELEMENTS * sizeof(uint64_t))));
	uint64_t mask __attribute__ ((vector_size(VEC_ELEMENTS * sizeof(uint64_t))));
	uint64_t init[VEC_ELEMENTS];
	size_t i;
	uint64_t xsum = 0;

	for (i = 0; i < VEC_ELEMENTS; i++)
		init[i] = i;
	(void)memcpy(&data, &init, sizeof(data));

	for (i = 0; i < VEC_ELEMENTS; i++)
		init[i] = (i + 3) & (VEC_ELEMENTS - 1);
	(void)memcpy(&mask, &init, sizeof(mask));

	data = __builtin_shuffle(data, mask);

	(void)memcpy(&init, &data, sizeof(init));

	for (i = 0; i < VEC_ELEMENTS; i++)
		xsum ^= init[i];

	return (int)xsum;
}

