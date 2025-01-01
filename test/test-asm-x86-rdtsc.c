/*
 * Copyright (C) 2024-2025 Colin Ian King
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

int main(void)
{
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)
	uint32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return (int)((uint64_t)hi << 32) | lo;

#elif defined(__i386__)   || defined(__i386)
	uint64_t tsc;

	__asm__ __volatile__("rdtsc" : "=A" (tsc));

	return (int)tsc;
#else
#error not an x86 so no rdtsc instruction
#endif
	return 0;
}
