// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#if defined(__sparc) ||		\
    defined(__sparc__) ||	\
    defined(__sparc_v9__)
int main(void)
{
	unsigned long ver;

	__asm__ __volatile__("rdpr %%ver, %0" : "=r" (ver));

	return 0;
}
#else
#error not SPARC so no rdpr instruction
#endif
