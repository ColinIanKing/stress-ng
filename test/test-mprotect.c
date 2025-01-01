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

#include <stdint.h>
#include <sys/mman.h>

static char buffer[8192];

int main(void)
{
	uintptr_t ptr = (((uintptr_t)buffer) & ~(4096 -1));
	int ret;

	ret = mprotect((void *)ptr, 4096, PROT_READ);
	(void)ret;
	ret = mprotect((void *)ptr, 4096, PROT_WRITE);
	(void)ret;
	ret = mprotect((void *)ptr, 4096, PROT_EXEC);
	(void)ret;
	ret = mprotect((void *)ptr, 4096, PROT_NONE);
	(void)ret;
	ret = mprotect((void *)ptr, 4096, PROT_READ | PROT_WRITE);
	(void)ret;

	return 0;
}
