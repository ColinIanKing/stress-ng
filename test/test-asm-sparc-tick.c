// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King
 *
 */
#include <stdint.h>

#if defined(__sparc) ||		\
    defined(__sparc__) ||	\
    defined(__sparc_v9__)
int main(void)
{
	register uint64_t ticks;

	__asm__ __volatile__("rd %%tick, %0"
			     : "=r" (ticks));

	return 0;
}
#else
#error not SPARC so no tick instruction
#endif
