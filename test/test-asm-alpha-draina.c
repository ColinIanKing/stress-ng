// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#if defined(__alpha) || defined(__alpha__)

#define PAL_draina	2

int main(void)
{
	__asm__ __volatile__("call_pal %0 #draina" : : "i" (PAL_draina) : "memory");

	return 0;
}
#else
#error not ALPHA so no draina instruction
#endif
