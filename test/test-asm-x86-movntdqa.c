/*
 * Copyright (C) 2026      Colin Ian King
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

static inline __uint128_t movntdqa(void *addr)
{
	__uint128_t ret;

	__asm__ __volatile__ (
		"movntdqa (%1),%0"
		: "=x" (ret)
		: "r" (addr)
		: "memory");
	return ret;
}

int main(int argc, char **argv)
{
	register __uint128_t val;
	static char buf[16] = {
		0xf0, 0xe1, 0xd2, 0xc3,
		0xb4, 0xa5, 0x96, 0x87,
		0x78, 0x69, 0x5a, 0x4b,
		0x3c, 0x2d, 0x1e, 0x0f,
	};

	val = movntdqa(buf);

	return (uint64_t)(val & 0xffffffffffffffffULL) == 0x8796a5b4c3d2e1f0ULL;
}
