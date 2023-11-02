// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include <stdint.h>

#if !defined(__PPC64__)
#error ppc64 dcbst instruction not supported
#endif

static inline void dcbst(void *addr)
{
	__asm__ __volatile__ ("dcbst %y0" : : "Z"(*(uint8_t *)addr) : "memory");
}

int main(void)
{
	static char buffer[1024];

	dcbst(buffer);
}
