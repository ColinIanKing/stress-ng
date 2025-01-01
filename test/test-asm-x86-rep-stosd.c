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

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)

static inline void repzero(void *ptr, const int n)
{
	__asm__ __volatile__(
		"mov $0xaaaaaaaa,%%eax\n;"
		"mov %0,%%rdi\n;"
		"mov %1,%%ecx\n;"
		"rep stosl %%eax,%%es:(%%rdi);\n"
		:
		: "m" (ptr),
		  "m" (n)
		: "ecx","rdi","eax");
}

int main(void)
{
	char buffer[1024];

	repzero(buffer, sizeof(buffer));
}
#else
#error not an x86 so no rep stosd instruction
#endif
