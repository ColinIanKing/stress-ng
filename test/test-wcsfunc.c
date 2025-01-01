/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#define _GNU_SOURCE

#if defined(__APPLE__) || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || \
    defined(__OpenBSD__)
#include <wchar.h>
#if !defined(__APPLE__)
#include <sys/tree.h>
#endif
#else
#include <bsd/stdlib.h>
#include <bsd/string.h>
#include <bsd/sys/tree.h>
#if !defined(__FreeBSD_kernel__)
/* GNU/kFreeBSD does not support this */
#include <bsd/wchar.h>
#endif
#endif

#include <stddef.h>
#include <wchar.h>

static void *funcs[] = {
	WCSFUNC,
};

int main(void)
{
	return (ptrdiff_t)(funcs[0] == 0);
}
