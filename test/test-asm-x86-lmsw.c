// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#include <stdint.h>

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__) || defined(__amd64) || 	\
    defined(__i386__)   || defined(__i386)
int main(int argc, char **argv)
{
	uint16_t src = 0;

	__asm__ __volatile__("lmsw %0" ::"r" (src));

	return 0;
}
#else
#error x86 lmsr instruction not supported
#endif
