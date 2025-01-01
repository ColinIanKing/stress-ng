/*
 * Copyright (C) 2021-2025 Colin Ian King
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
#include <xmmintrin.h>

int main(int argc, char **argv)
{
	__v2di data[4];
	__v2di val = { 0x123456789abcdefULL, 0xffeeddccbbaa9988 };

	__builtin_ia32_movntdq(&data[0], val);
	__builtin_ia32_movntdq(&data[1], val);
	__builtin_ia32_movntdq(&data[2], val);
	__builtin_ia32_movntdq(&data[3], val);

	return 0;
}
