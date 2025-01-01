/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#include <unistd.h>
#include <stdint.h>
#include <sys/mman.h>

static char buffer[8192];

#if defined(__gnu_hurd__)
#error mincore is defined but not implemented and will always fail
#endif

int main(void)
{
	unsigned char vec[1];
	uintptr_t ptr = (((uintptr_t)buffer) & ~(4096 -1));

	return mincore((void *)ptr, sizeof(vec), vec);
}
