/*
 * Copyright (C) 2021-2025 Colin Ian King
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

int main(int argc, char **argv)
{
	__uint128_t v128, data128 = ~0;
	uint64_t v64, data64 = ~0;
	uint32_t v32, data32 = ~0;

	v128 = __builtin_nontemporal_load(&data128);
	(void)v128;
	v64 = __builtin_nontemporal_load(&data64);
	(void)v64;
	v32 = __builtin_nontemporal_load(&data32);
	(void)v32;

	return 0;
}
