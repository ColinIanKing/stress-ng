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
	__int64 data[4];
	__int64 val = (__int64)main;

	_mm_stream_si64((__int64 *)&data[0], (__int64)val);
	_mm_stream_si64((__int64 *)&data[1], (__int64)~val);

	val = data[0] * data[1];
	val = (val >> 32) ^ (val & 0xffffffffUL);

	return (int)val;
}
#else
#error need ICC to build successfully
#endif
