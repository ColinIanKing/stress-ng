// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
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
