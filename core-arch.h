/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_ARCH_H
#define CORE_ARCH_H

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

/* Arch specific PPC64 */
#if defined(__PPC64__)
#define STRESS_ARCH_PPC64	(1)
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

/* Arch specific SPARC */
#if defined(__sparc) ||		\
    defined(__sparc__) ||	\
    defined(__sparc_v9__)
#define STRESS_ARCH_SPARC	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

/* Arch specific SH4 */
#if defined(__SH4__)
#define STRESS_ARCH_SH4		(1)
#define STRESS_OPCODE_SIZE	(16)
#define STRESS_OPCODE_MASK	(0xffffUL)
#endif

/* Arch specific ALPHA */
#if defined(__alpha) ||		\
    defined(__alpha__)
#define STRESS_ARCH_ALPHA	(1)
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

/* Arch specific MIPS */
#if defined(__mips) || 		\
    defined(__mips__) ||	\
    defined(_mips)
#define STRESS_ARCH_MIPS	(1)
#define STRESS_OPCODE_SIZE	(32)
#define STRESS_OPCODE_MASK	(0xffffffffUL)
#endif

#endif
