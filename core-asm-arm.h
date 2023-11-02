/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_ASM_ARM_H
#define CORE_ASM_ARM_H

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_ARM)

static inline void stress_asm_arm_yield(void)
{
	__asm__ __volatile__("yield;\n");
}

/* #if defined(STRESS_ARCH_ARM) */
#endif

/* #ifndef CORE_ASM_ARM_H */
#endif
