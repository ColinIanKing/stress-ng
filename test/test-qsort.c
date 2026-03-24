/*
 * Copyright (C)      2026 Colin Ian King
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
#include <stdlib.h>
#include <stdio.h>

static int cmp(const void *v1, const void *v2)
{
	int i1 = *(int *)v1;
	int i2 = *(int *)v2;

	if (i1 < i2)
		return -1;
	if (i1 > i2)
		return 1;
	return 0;
}

int data[10] = { 1, 10, 3, 2, 7, 9, 5, 8, 6, 4 };

int main(void)
{

	qsort(data, 10, sizeof(int), cmp);

	return data[0];
}
