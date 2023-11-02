// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

/* Arch specific M68K */
#if defined(__m68k__) ||	\
    defined(__mc68000__) ||	\
    defined(__mc68010__) ||	\
    defined(__mc68020__)
int main(void)
{
	__asm__ __volatile__("eori.w #0001,%sr");

	return 0;
}
#else
#error not m68k so no eori.w #0001,sr instruction
#endif
