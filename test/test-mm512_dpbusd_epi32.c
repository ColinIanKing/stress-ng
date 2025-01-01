/*
 * Copyright (C) 2023-2025 Colin Ian King
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
#include <immintrin.h>
#include <string.h>
#include <stdint.h>

void rndset(unsigned char *ptr, const size_t len)
{
	size_t i;
	uintptr_t addr = (uintptr_t)rndset;

	for (i = 0; i < len; i++, addr += 37)
		ptr[i] = (unsigned char)((addr >> 3) & 0xff);
}

int __attribute__ ((target("avx512vnni"))) main(int argc, char **argv)
{
	__m512i a, b, c, r;

	(void)rndset((unsigned char *)&a, sizeof(a));
	(void)rndset((unsigned char *)&b, sizeof(b));
	(void)rndset((unsigned char *)&c, sizeof(b));
	r = _mm512_dpbusd_epi32(c, a, b);

	return *(int *)&r;
}
