/*
 * Copyright (C) 2023      Colin Ian King.
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
