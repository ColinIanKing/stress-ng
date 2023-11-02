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
	uint32_t lo, hi, counter = 0;

	__asm__ __volatile__("rdpmc" : "=a" (lo), "=d" (hi) : "c" (counter));
}
#else
#error x86 rdpmc instruction not supported
#endif
