// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#if defined(__alpha) || defined(__alpha__)

#define PAL_halt	0

int main(void)
{
	__asm__ __volatile__("call_pal %0 #halt" : : "i" (PAL_halt));

	return 0;
}
#else
#error not ALPHA so no halt instruction
#endif
