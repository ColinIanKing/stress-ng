// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */

#if !defined(__PPC64__)
#error ppc64 darn instruction not supported
#endif

int main(void)
{
	__asm__ __volatile__ ("msync" : : : "memory");
}
