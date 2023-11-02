/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_S390_H
#define CORE_ASM_S390_H

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_S390)

static inline uint64_t stress_asm_s390_stck(void)
{
	uint64_t tick;

	__asm__ __volatile__("\tstck\t%0\n" : "=Q" (tick) : : "cc");

	return tick;
}

/* #if defined(STRESS_ARCH_S390) */
#endif

/* #ifndef CORE_ASM_S390_H */
#endif
