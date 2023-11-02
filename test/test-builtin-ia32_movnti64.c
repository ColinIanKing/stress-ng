// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <xmmintrin.h>

int main(int argc, char **argv)
{
	long long int data[4];
	long long int val = 0x123456789abcdefULL;

	__builtin_ia32_movnti64(&data[0], val);
	__builtin_ia32_movnti64(&data[1], val);
	__builtin_ia32_movnti64(&data[2], val);
	__builtin_ia32_movnti64(&data[3], val);

	return 0;
}
