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
	__asm__ __volatile__("wbinvd");

	return 0;
}
#else
#error x86 wbinvd instruction not supported
#endif
