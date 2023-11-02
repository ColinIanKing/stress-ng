// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#if defined(__mips) ||		\
    defined(__mips__) ||	\
    defined(_mips)
int main(void)
{
	__asm__ __volatile__("wait");

	return 0;
}
#else
#error not MIPS so no wait instruction
#endif
