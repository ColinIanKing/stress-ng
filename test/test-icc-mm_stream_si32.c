// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
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
