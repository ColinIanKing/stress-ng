// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__) || defined(__amd64) || 	\
    defined(__i386__)   || defined(__i386)
int main(int argc, char **argv)
{
	char data[4096];

	__asm__ __volatile__("lgdt (%0)" ::"r" (data));

	return 0;
}
#else
#error x86 lgdt instruction not supported
#endif
