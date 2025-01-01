/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include <string.h>
#if defined(__APPLE__) || \
    defined(__DragonFly__) || \
    defined(__FreeBSD__) || \
    defined(__NetBSD__) || \
    defined(__OpenBSD__)
#include <stdlib.h>
#else
#include <bsd/stdlib.h>
#endif

static int intcmp(const void *p1, const void *p2)
{
        const int *i1 = (int *)p1;
        const int *i2 = (int *)p2;

	return *i1 - *i2;
}

int main(void)
{
	int data[64];
	int rc;

	(void)memset(data, 0, sizeof(data));

	rc = heapsort(data, 64, sizeof(*data), intcmp);
	(void)rc;
	rc = mergesort(data, 64, sizeof(*data), intcmp);
	(void)rc;
	rc = radixsort(NULL, 0, NULL, 0);
	(void)rc;

	return 0;
}
