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
#ifndef CORE_ASM_LOONG64_H
#define CORE_ASM_LOONG64_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(STRESS_ARCH_LOONG64)

#if defined(HAVE_ASM_LOONG64_RDTIME)
static inline uint64_t ALWAYS_INLINE stress_asm_loong64_rdtime(void)
{
	uint64_t val = 0;

	__asm__ __volatile__("rdtime.d %0, $zero\n\t" : "=r"(val) :);

	return val;
}
#endif

#if defined(HAVE_ASM_LOONG64_DBAR)
static inline void ALWAYS_INLINE stress_asm_loong64_dbar(void)
{
	__asm__ __volatile__("dbar 0" ::: "memory");
}
#endif

#if defined(HAVE_ASM_LOONG64_CPUCFG)
static inline uint32_t ALWAYS_INLINE stress_asm_loong64_cpucfg(const uint32_t cfg)
{
	uint32_t ret;

	__asm__ __volatile__(
	"cpucfg %1, %0\n"
        : "=r" (ret)
        : "r" (cfg)
        : "memory");

	return ret;
}
#endif

/* STRESS_ARCH_LOONG64 */
#endif

/* CORE_ASM_LOONG64_H */
#endif
