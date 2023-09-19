// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#if defined(__ICC)
#include <immintrin.h>

int main(int argc, char **argv)
{
	__uint128_t data[2];
	__uint128_t val = (__uint128_t)main | (((__uint128_t)main) << 64);

	_mm_stream_si128((__m128i *)&data[0], (__m128i)val);
	_mm_stream_si128((__m128i *)&data[1], (__m128i)~val);

	val = data[0] * data[1];
	val = (val >> 64) ^ (val & 0xffffffffffffffffULL);
	val = (val >> 32) ^ (val & 0xffffffffUL);

	return (int)val;
}
#else
#error need ICC to build successfully
#endif
