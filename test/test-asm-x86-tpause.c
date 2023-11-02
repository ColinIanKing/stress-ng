// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdint.h>

#if defined(__x86_64__) || defined(__x86_64) ||	\
    defined(__amd64__)  || defined(__amd64)
int main(void)
{
	uint32_t lo = 0xffffffff, hi = 0xffffffff, ecx = 0;

	__asm__ __volatile__("tpause %%ecx\n" :: "c"(ecx), "d"(hi), "a"(lo));

	return 0;
}
#else
#error not an x86 so no tpause instruction
#endif

