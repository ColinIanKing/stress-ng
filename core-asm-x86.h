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
#ifndef CORE_ASM_X86_H
#define CORE_ASM_X86_H

#include "stress-ng.h"

#if defined(HAVE_ASM_X86_PAUSE)
static inline void stress_asm_x86_pause(void)
{
	__asm__ __volatile__("pause;\n" ::: "memory");
}
#endif

#if defined(HAVE_ASM_X86_TPAUSE)
static inline void stress_asm_x86_tpause(uint32_t ecx, uint32_t delay)
{
	uint32_t lo, hi;
	uint64_t val;

	__asm__ __volatile__("rdtsc\n" : "=a"(lo),"=d"(hi));
	val = (((uint64_t)hi << 32) | lo) + delay;
	lo = val & 0xffffffff;
	hi = val >> 32;
	__asm__ __volatile__("tpause %%ecx\n" :: "c"(ecx), "d"(hi), "a"(lo));
}
#endif

#if defined(HAVE_ASM_X86_SERIALIZE)
static inline void stress_asm_x86_serialize(void)
{
	__asm__ __volatile__("serialize");
}
#endif

#endif
