/*
 * Copyright (C) 2025      Colin Ian King
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

static int cmpint(const void *p1, const void *p2)
{
	int *i1 = (int *)p1;
	int *i2 = (int *)p2;

	if (*i1 < *i2)
		return -1;
	else if (*i1 > *i2)
		return 1;
	return 0;
}

int main(void)
{
	int data[] = { 3, 2, 4, 1, 5 };

	return mergesort(data, 5, sizeof(int), cmpint);
}
