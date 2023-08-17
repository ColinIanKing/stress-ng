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
	__m512i r;
	unsigned char a[512];

	(void)rndset((unsigned char *)&r, sizeof(r));
	_mm512_storeu_si512((void *)a, r);

	return *(int *)&r;
}
