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

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__) || defined(__amd64) || 	\
    defined(__i386__)   || defined(__i386)
int main(int argc, char **argv)
{
	unsigned long int dr0;

	__asm__ __volatile__("mov %%dr0, %0" : "=r"(dr0) : : "memory");

	return 0;
}
#else
#error x86 mov cr0 instruction not supported
#endif
