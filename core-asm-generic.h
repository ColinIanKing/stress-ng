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
#ifndef CORE_ASM_GENERIC_H
#define CORE_ASM_GENERIC_H

static inline void ALWAYS_INLINE stress_asm_nop(void)
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

static inline void ALWAYS_INLINE stress_asm_mb(void)
{
#if defined(HAVE_ASM_MB)
        __asm__ __volatile__("" ::: "memory");
#endif
}

static inline void ALWAYS_INLINE stress_asm_nothing(void)
{
#if defined(HAVE_ASM_NOTHING)
	__asm__ __volatile__("");
#endif
}

#endif
