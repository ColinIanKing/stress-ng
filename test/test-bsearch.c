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
#include <search.h>
#include <stdlib.h>

static int cmp(const void *p1, const void *p2)
{
	const int *i1 = (const int *)p1;
	const int *i2 = (const int *)p2;

	if (*i1 > *i2)
		return 1;
	else if (*i1 < *i2)
		return -1;
	else
		return 0;
}

int main(void)
{
	int data[] = { 1, 2, 4, 8 };
	int val = 2;
	void *ptr;

	ptr = bsearch((void *)&val, (void *)data, 4, sizeof(data[0]), cmp);

	return ptr == &data[1];
}
