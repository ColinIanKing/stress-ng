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

int __attribute__ ((target("avxvnni"))) main(int argc, char **argv)
{
	__m128i r;
	unsigned char a[128];

	(void)rndset((unsigned char *)&a, sizeof(a));
	r = _mm_loadu_si128((void *)a);

	return *(int *)&r;
}
