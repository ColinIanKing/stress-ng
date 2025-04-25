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
#ifndef CORE_ASM_S390_H
#define CORE_ASM_S390_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(STRESS_ARCH_S390)

static inline uint64_t ALWAYS_INLINE stress_asm_s390_stck(void)
{
	uint64_t tick;

	__asm__ __volatile__("\tstck\t%0\n" : "=Q" (tick) : : "cc");

	return tick;
}

/* #if defined(STRESS_ARCH_S390) */
#endif

/* #ifndef CORE_ASM_S390_H */
#endif
