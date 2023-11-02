// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <xmmintrin.h>

int main(int argc, char **argv)
{
	int data[4];
	int val = 0x1234567;

	__builtin_ia32_movnti(&data[0], val);
	__builtin_ia32_movnti(&data[1], val);
	__builtin_ia32_movnti(&data[2], val);
	__builtin_ia32_movnti(&data[3], val);

	return 0;
}
