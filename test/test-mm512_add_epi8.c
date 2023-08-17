// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#include <immintrin.h>
#include <string.h>
#include <stdlib.h>

void rndset(unsigned char *ptr, const size_t len)
{
	size_t i;

	for (i = 0; i < len; i++)
		ptr[i] = random();
}

int __attribute__ ((target("avx512bw"))) main(int argc, char **argv)
{
	__m512i a, b, r;

	(void)rndset((unsigned char *)&a, sizeof(a));
	(void)rndset((unsigned char *)&b, sizeof(b));
	r = _mm512_add_epi8(a, b);

	return *(int *)&r;
}
