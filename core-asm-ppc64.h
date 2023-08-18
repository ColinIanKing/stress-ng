/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_PPC64_H
#define CORE_ASM_PPC64_H

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_PPC64)

#if defined(HAVE_ASM_PPC64_DARN)
static inline uint64_t stress_asm_ppc64_darn(void)
{
	uint64_t val;

	/* Unconditioned raw deliver a raw number */
	__asm__ __volatile__("darn %0, 0\n" : "=r"(val) :);
	return val;
}
#endif

#if defined(HAVE_ASM_PPC64_DCBST)
static inline void stress_asm_ppc64_dcbst(void *addr)
{
	__asm__ __volatile__("dcbst %y0" : : "Z"(*(uint8_t *)addr) : "memory");
}
#endif

#if defined(HAVE_ASM_PPC64_DCBT)
static inline void stress_asm_ppc64_dcbt(void *addr)
{
	__asm__ __volatile__("dcbt 0,%0" : : "r"(addr));
}
#endif

#if defined(HAVE_ASM_PPC64_DCBTST)
static inline void stress_asm_ppc64_dcbtst(void *addr)
{
	__asm__ __volatile__("dcbtst 0,%0" : : "r"(addr));
}
#endif

#if defined(HAVE_ASM_PPC64_ICBI)
static inline void stress_asm_ppc64_icbi(void *addr)
{
	__asm__ __volatile__("icbi %y0" : : "Z"(*(uint8_t *)addr) : "memory");
}
#endif


#if defined(HAVE_ASM_PPC64_MSYNC)
static inline void stress_asm_ppc64_msync(void)
{
	__asm__ __volatile__ ("msync" : : : "memory");
}
#endif

static inline void stress_asm_ppc64_yield(void)
{
	__asm__ __volatile__("or 27,27,27;\n");
}

static inline void stress_asm_ppc64_mdoio(void)
{
	__asm__ __volatile__("or 29,29,29;\n");
}

static inline void stress_asm_ppc64_mdoom(void)
{
	__asm__ __volatile__("or 30,30,30;\n");
}

/* #if defined(STRESS_ARCH_PPC64) */
#endif

/* #ifndef CORE_ASM_PPC64_H */
#endif
