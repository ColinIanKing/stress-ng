/*
 * Copyright (C) 2024-2025 Colin Ian King.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 */
#ifndef CORE_ASM_SPARC_H
#define CORE_ASM_SPARC_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(STRESS_ARCH_SPARC)

#if defined(HAVE_ASM_SPARC_TICK)
static inline uint64_t ALWAYS_INLINE stress_asm_sparc_tick(void)
{
	register uint64_t ticks;

	__asm__ __volatile__("rd %%tick, %0"
			     : "=r" (ticks));
	return (uint64_t)ticks;
}
#endif

#if defined(HAVE_ASM_SPARC_MEMBAR)
static inline void ALWAYS_INLINE stress_asm_sparc_membar(void)
{
         __asm__ __volatile__ ("membar #StoreLoad" : : : "memory");
}
#endif

/* #if defined(STRESS_ARCH_SPARC) */
#endif

/* #ifndef CORE_ASM_SPARC_H */
#endif
