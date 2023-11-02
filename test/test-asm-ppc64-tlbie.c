// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <stdint.h>

#if !defined(__PPC64__)
#error ppc64 tlbie instruction not supported
#endif

static inline void tlbie(void *addr)
{
	unsigned long address = (unsigned long)addr;

	__asm__ __volatile__("tlbie %0, 0" : : "r" (address) : "memory");
}

int main(void)
{
	tlbie(main);
}
