/*
 * Copyright (C) 2024-2025 Colin Ian King.
 * Copyright (C) 2025 SiPearl
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
#ifndef CORE_ASM_ARM_H
#define CORE_ASM_ARM_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(STRESS_ARCH_ARM)

#if defined(HAVE_ASM_ARM_PRFM)
/* KEEP */
static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl1keep(void *p)
{
       __asm__ __volatile__("prfm PLDL1KEEP, [%0]\n" : : "r"(p) : "memory");
}

static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl2keep(void *p)
{
       __asm__ __volatile__("prfm PLDL2KEEP, [%0]\n" : : "r"(p) : "memory");
}

static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl3keep(void *p)
{
       __asm__ __volatile__("prfm PLDL3KEEP, [%0]\n" : : "r"(p) : "memory");
}

/* STRM */
static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl1strm(void *p)
{
       __asm__ __volatile__("prfm PLDL1STRM, [%0]\n" : : "r"(p) : "memory");
}

static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl2strm(void *p)
{
       __asm__ __volatile__("prfm PLDL2STRM, [%0]\n" : : "r"(p) : "memory");
}

static inline void ALWAYS_INLINE stress_asm_arm_prfm_pldl3strm(void *p)
{
       __asm__ __volatile__("prfm PLDL3STRM, [%0]\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_ARM_YIELD)
static inline void ALWAYS_INLINE stress_asm_arm_yield(void)
{
	__asm__ __volatile__("yield;\n");
}
#endif

#if defined(HAVE_ASM_ARM_DMB_SY)
static inline void ALWAYS_INLINE stress_asm_arm_dmb_sy(void)
{
	__asm__ __volatile__("dmb sy;\n");
}
#endif

/* #if defined(STRESS_ARCH_ARM) */
#endif

/* #ifndef CORE_ASM_ARM_H */
#endif
