// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#if !defined(__PPC64__)
#error ppc64 dcbtst instruction not supported
#endif

static inline void dcbtst(void *addr)
{
	__asm__ __volatile__("dcbtst 0,%0" : : "r"(addr));
}

int main(void)
{
	static char buffer[1024];

	dcbtst(buffer);
}
