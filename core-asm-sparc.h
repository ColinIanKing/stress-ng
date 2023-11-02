/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_SPARC_H
#define CORE_ASM_SPARC_H

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_SPARC)

#if defined(HAVE_ASM_SPARC_TICK)
static inline uint64_t stress_asm_sparc_tick(void)
{
	register uint64_t ticks;

	__asm__ __volatile__("rd %%tick, %0"
			     : "=r" (ticks));
	return (uint64_t)ticks;
}
#endif

#if defined(HAVE_ASM_SPARC_MEMBAR)
static inline void stress_asm_sparc_membar(void)
{
         __asm__ __volatile__ ("membar #StoreLoad" : : : "memory");
}
#endif

/* #if defined(STRESS_ARCH_SPARC) */
#endif

/* #ifndef CORE_ASM_SPARC_H */
#endif
