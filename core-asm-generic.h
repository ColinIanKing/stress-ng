/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_GENERIC_H
#define CORE_ASM_GENERIC_H

static inline void stress_asm_nop(void)
{
#if defined(HAVE_ASM_NOP)
#if defined(STRESS_ARCH_KVX)
	/*
	 * Extra ;; required for KVX to indicate end of
	 * a VLIW instruction bundle
	 */
	__asm__ __volatile__("nop\n;;\n");
#else
	__asm__ __volatile__("nop;\n");
#endif
#endif
}

static inline void stress_asm_mb(void)
{
#if defined(HAVE_ASM_MB)
        __asm__ __volatile__("" ::: "memory");
#endif
}

static inline void stress_asm_nothing(void)
{
#if defined(HAVE_ASM_NOTHING)
	__asm__ __volatile__("");
#endif
}

#endif
