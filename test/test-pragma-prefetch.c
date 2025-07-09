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
#include <stdio.h>

#define STRESS_PRAGMA_PREFETCH          _Pragma("GCC optimize (\"prefetch-loop-arrays\")")
#define STRESS_PRAGMA_NOPREFETCH        _Pragma("GCC optimize (\"no-prefetch-loop-arrays\")")

STRESS_PRAGMA_PREFETCH
static int data_sum_prefetch(unsigned char *data, int len)
{
	int i;
	int sum = 0;

	for (i = 0; i < len; i++) {
		sum += data[i];
	}
	return sum;
}

STRESS_PRAGMA_NOPREFETCH
static int data_sum_noprefetch(unsigned char *data, int len)
{
	int i;
	int sum = 0;

	for (i = 0; i < len; i++) {
		sum += data[i];
	}
	return sum;
}

int main(int argc, char **argv)
{
	static unsigned char data[16384];
	int i;

	for (i = 0; i < (int)sizeof(data); i++)
		data[i] = i + argc;

	printf("%d %d\n", data_sum_prefetch(data, sizeof(data)), data_sum_noprefetch(data, sizeof(data)));

	return 0;
}
