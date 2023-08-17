// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
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
