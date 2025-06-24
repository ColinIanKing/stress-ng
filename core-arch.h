/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#ifndef CORE_ARCH_H
#define CORE_ARCH_H

#include "stress-ng.h"

extern WARN_UNUSED const char *stress_get_arch(void);

#if defined(__BYTE_ORDER__) &&  \
    defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__
#define STRESS_ARCH_LE
#endif
#endif

#if defined(__BYTE_ORDER__) &&  \
    defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_BIG_ENDIAN__
#define STRESS_ARCH_BE
#endif
#endif

/* Arch specific ALPHA */
#if defined(__alpha) ||		\
    defined(__alpha__)
#define STRESS_ARCH_ALPHA	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific, ARC64 */
#if defined(__ARC64__) || defined(__ARC64)
#define STRESS_ARCH_ARC64
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific, ARM */
#if defined(__ARM_ARCH_6__)   || defined(__ARM_ARCH_6J__)  || \
    defined(__ARM_ARCH_6K__)  || defined(__ARM_ARCH_6Z__)  || \
    defined(__ARM_ARCH_6ZK__) || defined(__ARM_ARCH_6T2__) || \
    defined(__ARM_ARCH_6M__)  || defined(__ARM_ARCH_7__)   || \
    defined(__ARM_ARCH_7A__)  || defined(__ARM_ARCH_7R__)  || \
    defined(__ARM_ARCH_7M__)  || defined(__ARM_ARCH_7EM__) || \
    defined(__ARM_ARCH_8A__)  || defined(__aarch64__)
#define STRESS_ARCH_ARM		(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific HPPA  */
#if defined(__hppa) ||		\
    defined(__hppa__)
#define STRESS_ARCH_HPPA	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#undef HAVE_SIGALTSTACK
#endif

/* Arch specific Kalray VLIW core */
#if defined(__KVX__) ||		\
    defined(__kvx__)
#define STRESS_ARCH_KVX		(1)
#endif

/* Arch specific LoongArch64 */
#if defined(__loongarch64) ||	\
    defined(__loongarch__)
#define STRESS_ARCH_LOONG64	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific M68K */
#if defined(__m68k__) ||	\
    defined(__mc68000__) ||	\
    defined(__mc68010__) ||	\
    defined(__mc68020__)
#define STRESS_ARCH_M68K	(1)
#define STRESS_OPCODE_SIZE	(16)
#define STRESS_OPCODE_MASK	(0xffffUL)
#endif

/* Arch specific MIPS */
#if defined(__mips) || 		\
    defined(__mips__) ||	\
    defined(_mips)
#define STRESS_ARCH_MIPS	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific PPC64, must be before PPC */
#if defined(__PPC64__) || defined(__ppc64__)
#define STRESS_ARCH_PPC64	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific PPC (32 bit ) */
#if (defined(__PPC__) || defined(__ppc__)) &&	\
    !defined(STRESS_ARCH_PPC64)
#define STRESS_ARCH_PPC		(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific RISC-V */
#if defined(__riscv) || \
    defined(__riscv__)
#define STRESS_ARCH_RISCV	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific, IBM S390 */
#if defined(__s390__)
#define STRESS_ARCH_S390	(1)
#define STRESS_OPCODE_SIZE	(48)
#define STRESS_OPCODE_MASK	(0xffffffffffffULL)
#endif

/* Arch specific SH4 */
#if defined(__SH4__)
#define STRESS_ARCH_SH4		(1)
#define STRESS_OPCODE_SIZE	(16)
#define STRESS_OPCODE_MASK	(0xffffUL)
#endif

/* Arch specific SPARC */
#if defined(__sparc) ||		\
    defined(__sparc__) ||	\
    defined(__sparc_v9__)
#define STRESS_ARCH_SPARC	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific, x86-64 */
#if defined(__x86_64__) || defined(__x86_64) || \
    defined(__amd64__) || defined(__amd64)
#define STRESS_ARCH_X86		(1)
#define STRESS_ARCH_X86_64	(1)
#endif

/* Arch specific, x86-32 (i386 et al) */
#if defined(__i386__)   || defined(__i386)
#define STRESS_ARCH_X86		(1)
#define STRESS_ARCH_X86_32	(1)
#endif

#endif
