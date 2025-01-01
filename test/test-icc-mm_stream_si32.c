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
#if defined(__ICC)
#include <immintrin.h>

int main(int argc, char **argv)
{
	int data[4];
	int val = (int)sizeof(main);

	_mm_stream_si32(&data[0], val);
	_mm_stream_si32(&data[1], ~val);

	val = data[0] * data[1];

	return val;
}
#else
#error need ICC to build successfully
#endif
