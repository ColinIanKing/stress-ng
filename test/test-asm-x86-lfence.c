// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)  || \
    defined(__i386__)   || defined(__i386)
int main(void)
{
	__asm__ __volatile__("lfence" : : : "memory");

	return 0;
}
#else
#error not an x86 so no mfence instruction
#endif
