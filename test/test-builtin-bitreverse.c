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
#include <stdint.h>

int main(int argc, char **argv)
{
	uintptr_t addr = (uintptr_t)main;

	const uint64_t r = (uint64_t)addr;
	uint8_t u8 = __builtin_bitreverse8(r & 0xff);
	uint16_t u16 = __builtin_bitreverse16(r & 0xffff);
	uint32_t u32 = __builtin_bitreverse32(r & 0xffffffffUL);
	uint64_t u64 = __builtin_bitreverse64(r);

	return (int)u64 | (int)u32 | (int)u16 | (int)u8;
}
