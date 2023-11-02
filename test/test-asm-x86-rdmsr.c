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
	uint32_t msr = 0xc0000080;
	uint32_t lo;
	uint32_t hi;

	__asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));

	return 0;
}
#else
#error x86 rdmsr instruction not supported
#endif
