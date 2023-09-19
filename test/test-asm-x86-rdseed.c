// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include <stdint.h>

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)  || \
    defined(__i386__)   || defined(__i386)
int main(void)
{
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__)  || defined(__amd64)
	uint64_t ret;
#endif
#if defined(__i386__)   || defined(__i386)
	uint32_t ret;
#endif

	__asm__ __volatile__("1:;\n\
		     rdseed %0;\n\
		     jnc 1b;\n":"=r"(ret));
	return 0;
}
#else
#error not an x86 so no rdseed instruction
#endif
