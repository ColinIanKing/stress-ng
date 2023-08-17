/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_RISCV_H
#define CORE_ASM_RISCV_H

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_RISCV)

static inline uint64_t stress_asm_riscv_rdtime(void)
{
	register unsigned long ticks;

        __asm__ __volatile__("rdtime %0"
                              : "=r" (ticks)
			      :
                              : "memory");
	return (uint64_t)ticks;
}

#if defined(HAVE_ASM_RISCV_FENCE)
static inline void stress_asm_riscv_fence(void)
{
         __asm__ __volatile__("fence" ::: "memory");
}
#endif

/* Flush instruction cache */
#if defined(HAVE_ASM_RISCV_FENCE_I)
static inline void stress_asm_riscv_fence_i(void)
{
         __asm__ __volatile__("fence.i" ::: "memory");
}
#endif

/* #if defined(STRESS_ARCH_RISCV) */
#endif

/* #ifndef CORE_ASM_RISCV_H */
#endif
