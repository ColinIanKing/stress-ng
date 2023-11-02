// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */

#if !defined(__PPC64__)
#error ppc64 darn instruction not supported
#endif

static inline unsigned long rand64(void)
{
	unsigned long val;

	/* Unconditioned raw deliver a raw number */
	__asm__ __volatile__("darn %0, 0\n" : "=r"(val) :);
	return val;
}

int main(void)
{
	return (int)rand64();
}
