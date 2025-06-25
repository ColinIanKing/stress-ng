/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-put.h"
#include "core-target-clones.h"

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_INT128_T)
#define CPU_X86_MMX	(0x00000001)
#define CPU_X86_SSE	(0x00000002)
#endif

static const stress_help_t help[] = {
	{ NULL,	"regs N",	"start N workers exercising CPU generic registers" },
	{ NULL,	"regs-ops N",	"stop after N x 1000 rounds of register shuffling" },
	{ NULL,	NULL,		NULL }
};

#if (defined(HAVE_COMPILER_GCC_OR_MUSL) && NEED_GNUC(8, 0, 0)) &&	\
    !defined(HAVE_COMPILER_CLANG) &&					\
    !defined(HAVE_COMPILER_ICC) &&					\
    !defined(HAVE_COMPILER_PCC) &&					\
    !defined(HAVE_COMPILER_TCC)

static int stress_regs_success;
static volatile uint32_t stash32;
static volatile uint64_t stash64;
#if defined(HAVE_INT128_T)
static volatile __uint128_t stash128;
#endif

#if defined(STRESS_ARCH_X86_64)	&&	\
    defined(HAVE_INT128_T)
static int x86_cpu_flags;
#endif

#define SHUFFLE_REGS16()	\
do {				\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
	SHUFFLE_REGS();		\
} while (0);

static void regs_check32(
	stress_args_t *args,
	const char *reg,
	const uint32_t expected,
	const uint32_t value)
{
	if (expected != value) {
		pr_fail("%s: register %s was 0x%"
			PRIx32 ", expecting 0x%" PRIx32 "\n",
			args->name, reg, expected, value);
		stress_regs_success = false;
	}
}

static void regs_check64(
	stress_args_t *args,
	const char *reg,
	const uint64_t expected,
	const uint64_t value)
{
	if (expected != value) {
		pr_fail("%s: register %s was 0x%"
			PRIx64 ", expecting 0x%" PRIx64 "\n",
			args->name, reg, expected, value);
		stress_regs_success = false;
	}
}

#if defined(HAVE_INT128_T)
static void regs_check128(
	stress_args_t *args,
	const char *reg,
	const __uint128_t expected,
	const __uint128_t value)
{
	if (expected != value) {
		static __uint128_t mask64 = 0xffffffffffffffffULL;
		pr_fail("%s: register %s was 0x%" PRIx64 "%16.16" PRIx64
			", expecting 0x%" PRIx64 "%16.16" PRIx64 "\n",
			args->name, reg,
			(uint64_t)(expected >> 64), (uint64_t)(expected & mask64),
			(uint64_t)(value >> 64), (uint64_t)(value & mask64));
	}
}
#else
#define regs_check128(args, reg, expected, value) 		\
do {								\
	(void)(args);						\
	(void)(reg);						\
	(void)(expected);					\
	(void)(value);						\
} while (0)
#endif

#define REGS_CHECK(args, reg, expected, value)			\
do {								\
	if (sizeof(value) == sizeof(uint32_t)) 			\
		regs_check32(args, reg, expected, value);	\
	else if (sizeof(value) == sizeof(uint64_t))		\
		regs_check64(args, reg, expected, value);	\
	else if (sizeof(value) == (sizeof(uint64_t) << 1))	\
		regs_check128(args, reg, expected, value);	\
} while (0);

#if defined(STRESS_ARCH_X86_64)

#define STRESS_REGS_HELPER

#if defined(HAVE_INT128_T)
static void OPTIMIZE0 stress_regs_helper_mmx(stress_args_t *args, register uint64_t v)
{
	__uint128_t v128 = ((__uint128_t)v << 64) | (v ^ 0xa55a5555aaaaULL);
	register __uint128_t xmm0 __asm__("xmm0") = v128;
	register __uint128_t xmm1 __asm__("xmm1") = xmm0 >> 1;
	register __uint128_t xmm2 __asm__("xmm2") = xmm0 << 1;
	register __uint128_t xmm3 __asm__("xmm3") = xmm0 >> 2;
	register __uint128_t xmm4 __asm__("xmm4") = xmm0 << 2;
	register __uint128_t xmm5 __asm__("xmm5") = ~xmm0;
	register __uint128_t xmm6 __asm__("xmm6") = ~xmm1;
	register __uint128_t xmm7 __asm__("xmm7") = ~xmm2;

#define SHUFFLE_REGS()	\
do {			\
	xmm7 = xmm0;	\
	xmm0 = xmm1;	\
	xmm1 = xmm2;	\
	xmm2 = xmm3;	\
	xmm3 = xmm4;	\
	xmm4 = xmm5;	\
	xmm5 = xmm6;	\
	xmm6 = xmm7;	\
} while (0);

	SHUFFLE_REGS16();

	stash128 = xmm5;
	REGS_CHECK(args, "xmm5", v128, stash128);

	stash128 = xmm0 + xmm1 + xmm2 + xmm3 +
		   xmm4 + xmm5 + xmm6 + xmm7;

#undef SHUFFLE_REGS
}
#endif

#if defined(HAVE_INT128_T)
static void OPTIMIZE0 stress_regs_helper_sse(stress_args_t *args, register uint64_t v)
{
	__uint128_t v128 = ((__uint128_t)v << 64) | (v ^ 0xa55a5555aaaaULL);
	register __uint128_t xmm0  __asm__("xmm0")  = v128;
	register __uint128_t xmm1  __asm__("xmm1")  = xmm0 >> 1;
	register __uint128_t xmm2  __asm__("xmm2")  = xmm0 << 1;
	register __uint128_t xmm3  __asm__("xmm3")  = xmm0 >> 2;
	register __uint128_t xmm4  __asm__("xmm4")  = xmm0 << 2;
	register __uint128_t xmm5  __asm__("xmm5")  = ~xmm0;
	register __uint128_t xmm6  __asm__("xmm6")  = ~xmm1;
	register __uint128_t xmm7  __asm__("xmm7")  = ~xmm2;
	register __uint128_t xmm8  __asm__("xmm8")  = ~xmm3;
	register __uint128_t xmm9  __asm__("xmm9")  = ~xmm4;
	register __uint128_t xmm10 __asm__("xmm10") = xmm0 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register __uint128_t xmm11 __asm__("xmm11") = xmm1 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register __uint128_t xmm12 __asm__("xmm12") = xmm2 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register __uint128_t xmm13 __asm__("xmm13") = xmm3 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register __uint128_t xmm14 __asm__("xmm14") = xmm4 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register __uint128_t xmm15 __asm__("xmm15") = xmm0 ^ 0xaa55aa55aa55aa55ULL;

#define SHUFFLE_REGS()	\
do {			\
	xmm15 = xmm0;	\
	xmm0  = xmm1;	\
	xmm1  = xmm2;	\
	xmm2  = xmm3;	\
	xmm3  = xmm4;	\
	xmm4  = xmm5;	\
	xmm5  = xmm6;	\
	xmm6  = xmm7;	\
	xmm7  = xmm8;	\
	xmm8  = xmm9;	\
	xmm9  = xmm10;	\
	xmm10 = xmm11;	\
	xmm11 = xmm12;	\
	xmm12 = xmm13;	\
	xmm13 = xmm14;	\
	xmm14 = xmm15;	\
} while (0);

	SHUFFLE_REGS16();

	stash128 = xmm14;
	REGS_CHECK(args, "xmm5", v128, stash128);

	stash128 = xmm0 + xmm1 + xmm2 + xmm3 +
		   xmm4 + xmm5 + xmm6 + xmm7 +
		   xmm8 + xmm9 + xmm10 + xmm11 +
		   xmm12 + xmm13 + xmm14 + xmm15;

#undef SHUFFLE_REGS
}
#endif

/*
 *  stress_regs_helper(void)
 *	stress x86_64 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t rax __asm__("rax") = v;
	register uint64_t rbx __asm__("rbx") = rax >> 1;
	register uint64_t rcx __asm__("rcx") = rax << 1;
	register uint64_t rdx __asm__("rdx") = rax >> 2;
	register uint64_t rsi __asm__("rsi") = rax << 2;
	register uint64_t rdi __asm__("rdi") = ~rax;
	register uint64_t r8  __asm__("r8")  = ~rbx;
	register uint64_t r9  __asm__("r9")  = ~rcx;
	register uint64_t r10 __asm__("r10") = ~rdx;
	register uint64_t r11 __asm__("r11") = ~rsi;
	register uint64_t r12 __asm__("r12") = rax ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r13 __asm__("r13") = rbx ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r14 __asm__("r14") = rcx ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r15 __asm__("r15") = rdx ^ 0xa5a5a5a5a5a5a5a5ULL;

#define SHUFFLE_REGS()	\
do {			\
	r15 = rax;	\
	rax = rbx;	\
	rbx = rcx;	\
	rcx = rdx;	\
	rdx = rsi;	\
	rsi = rdi;	\
	rdi = r8;	\
	r8  = r9;	\
	r9  = r10;	\
	r10 = r11;	\
	r11 = r12;	\
	r12 = r13;	\
	r13 = r14;	\
	r14 = r15;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = r12;
	REGS_CHECK(args, "r12", v, stash64);

	stash64 = rax + rbx + rcx + rdx +
		rsi + rdi + r8  + r9  +
		r10 + r11 + r12 + r13 +
		r14 + r15;

#undef SHUFFLE_REGS

#if defined(HAVE_INT128_T)
	if (x86_cpu_flags & CPU_X86_SSE)
		stress_regs_helper_sse(args, v);
	else if (x86_cpu_flags & CPU_X86_MMX)
		stress_regs_helper_mmx(args, v);
#endif

}
#endif

#if defined(STRESS_ARCH_X86_32)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress i386 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;
	register uint32_t eax __asm__("eax") = v32;
	register uint32_t ecx __asm__("ecx") = eax >> 1;
	register uint32_t ebx __asm__("ebx") = eax << 1;
	register uint32_t edx __asm__("edx") = eax >> 2;

#define SHUFFLE_REGS()	\
do {			\
	edx = eax;	\
	eax = ebx;	\
	ebx = ecx;	\
	ecx = edx;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = edx;
	REGS_CHECK(args, "edx", v, stash32);

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_LOONG64)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress loong64 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint64_t v64 = (uint64_t)v;
	register uint64_t r4  __asm__("r4")  = v64;
	register uint64_t r5  __asm__("r5")  = r4 >> 1;
	register uint64_t r6  __asm__("r6")  = r4 << 1;
	register uint64_t r7  __asm__("r7")  = r4 >> 2;
	register uint64_t r8  __asm__("r8")  = r4 << 2;
	register uint64_t r9  __asm__("r9")  = ~r4;
	register uint64_t r10 __asm__("r10") = ~r5;
	register uint64_t r11 __asm__("r11") = ~r6;
	register uint64_t r12 __asm__("r12") = ~r7;
	register uint64_t r13 __asm__("r13") = ~r8;
	register uint64_t r14 __asm__("r14") = r4 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r15 __asm__("r15") = r5 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r16 __asm__("r16") = r6 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r17 __asm__("r17") = r7 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r18 __asm__("r18") = r8 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r19 __asm__("r19") = r4 ^ 0xaa55aa55aa55aa55ULL;
	register uint64_t r20 __asm__("r20") = r5 ^ 0xaa55aa55aa55aa55ULL;
	register uint64_t r21 __asm__("r21") = r6 ^ 0xaa55aa55aa55aa55ULL;
	register uint64_t r23 __asm__("r23") = r7 ^ 0xaa55aa55aa55aa55ULL;
	register uint64_t r24 __asm__("r24") = r8 ^ 0xaa55aa55aa55aa55ULL;
	register uint64_t r25 __asm__("r25") = r4 ^ 0x55aa55aa55aa55aaULL;
	register uint64_t r26 __asm__("r26") = r5 ^ 0x55aa55aa55aa55aaULL;
	register uint64_t r27 __asm__("r27") = r6 ^ 0x55aa55aa55aa55aaULL;
	register uint64_t r28 __asm__("r28") = r7 ^ 0x55aa55aa55aa55aaULL;
	register uint64_t r29 __asm__("r29") = r8 ^ 0x55aa55aa55aa55aaULL;
	register uint64_t r30 __asm__("r30") = shim_rol64(r4);
	register uint64_t r31 __asm__("r31") = shim_ror64(r4);

#define SHUFFLE_REGS()	\
do {			\
	r31 = r4;	\
	r4  = r5;	\
	r5  = r6;	\
	r6  = r7;	\
	r7  = r8;	\
	r8  = r9;	\
	r9  = r10;	\
	r10 = r11;	\
	r11 = r12;	\
	r12 = r13;	\
	r13 = r14;	\
	r14 = r15;	\
	r15 = r16;	\
	r16 = r17;	\
	r17 = r18;	\
	r18 = r19;	\
	r19 = r20;	\
	r20 = r21;	\
	r21 = r23;	\
	r23 = r24;	\
	r24 = r25;	\
	r25 = r26;	\
	r26 = r27;	\
	r27 = r28;	\
	r28 = r29;	\
	r29 = r30;	\
	r30 = r31;	\
} while (0);

	SHUFFLE_REGS16();

	stash64 = r14;
	REGS_CHECK(args, "r14", v, stash64);

	stash64 = r4 + r5 + r6 + r7 +
		r8 + r9 + r10 + r11 +
		r12 + r13 + r14 + r15 +
		r16 + r17 + r18 + r19 +
		r20 + r21 + r23 + r24 +
		r25 + r26 + r27 + r28 +
		r29 + r30 + r31;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_HPPA)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress hppa PA risc 1 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;
	register uint32_t r4  __asm__("r4")  = v32;
	register uint32_t r5  __asm__("r5")  = r4 >> 1;
	register uint32_t r6  __asm__("r6")  = r4 << 1;
	register uint32_t r7  __asm__("r7")  = r4 >> 2;
	register uint32_t r8  __asm__("r8")  = r4 << 2;
	register uint32_t r9  __asm__("r9")  = ~r4;
	register uint32_t r10 __asm__("r10") = ~r5;
	register uint32_t r11 __asm__("r11") = ~r6;
	register uint32_t r12 __asm__("r12") = ~r7;
	register uint32_t r13 __asm__("r13") = ~r8;
	register uint32_t r14 __asm__("r14") = r4 ^ 0xa5a5a5a5UL;
	register uint32_t r15 __asm__("r15") = r5 ^ 0xa5a5a5a5UL;
	register uint32_t r16 __asm__("r16") = r6 ^ 0xa5a5a5a5UL;
	register uint32_t r17 __asm__("r17") = r7 ^ 0xa5a5a5a5UL;
	register uint32_t r18 __asm__("r18") = r8 ^ 0xa5a5a5a5UL;

#define SHUFFLE_REGS()	\
do {			\
	r18 = r4;	\
	r4  = r5;	\
	r5  = r6;	\
	r6  = r7;	\
	r7  = r8;	\
	r8  = r9;	\
	r9  = r10;	\
	r10 = r11;	\
	r11 = r12;	\
	r12 = r13;	\
	r13 = r14;	\
	r14 = r15;	\
	r15 = r16;	\
	r16 = r17;	\
	r17 = r18;	\
} while (0);

	SHUFFLE_REGS16();

	stash32 = r16;
	REGS_CHECK(args, "r16", v, stash32);

	stash32 = r4 + r5 + r6 + r7 +
		r8 + r9 + r10 + r11 +
		r12 + r13 + r14 + r15 +
		r16 + r17 + r18;

#undef SHUFFLE_REGS
}
#endif


#if defined(STRESS_ARCH_M68K)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress m68000 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;
	register uint32_t d1 __asm__("d1") = v32;
	register uint32_t d2 __asm__("d2") = d1 >> 1;
	register uint32_t d3 __asm__("d3") = d1 << 1;
	register uint32_t d4 __asm__("d4") = d1 >> 2;
	register uint32_t d5 __asm__("d5") = d1 << 2;
	register uint32_t d6 __asm__("d6") = d1 << 2;

#define SHUFFLE_REGS()	\
do {			\
	d6 = d1;	\
	d1 = d2;	\
	d2 = d3;	\
	d3 = d4;	\
	d4 = d5;	\
	d5 = d6;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = d5;
	REGS_CHECK(args, "d5", v32, stash32);

	stash32 = d1 + d2 + d3 +
		d4 + d5 + d6;
#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_SH4)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress 32 bit sh4 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;
	register uint32_t r2 __asm__("r2") = v32;
	register uint32_t r3 __asm__("r3") = r2 >> 1;
	register uint32_t r4 __asm__("r4") = r2 << 1;
	register uint32_t r5 __asm__("r5") = r2 >> 2;
	register uint32_t r6 __asm__("r6") = r2 << 2;
	register uint32_t r7 __asm__("r7") = ~r2;
	register uint32_t r8 __asm__("r8") = ~r3;
	register uint32_t r9 __asm__("r9") = ~r4;
	register uint32_t r10 __asm__("r10") = ~r5;
	register uint32_t r11 __asm__("r11") = ~r6;
	register uint32_t r12 __asm__("r12") = r2 ^ 0xa5a5a5a5UL;
	register uint32_t r13 __asm__("r13") = r3 ^ 0xa5a5a5a5UL;

#define SHUFFLE_REGS()	\
do {			\
	r13 = r2;	\
	r2  = r3;	\
	r3  = r4;	\
	r4  = r5;	\
	r5  = r6;	\
	r6  = r7;	\
	r7  = r8;	\
	r8  = r9;	\
	r9  = r10;	\
	r10 = r11;	\
	r11 = r12;	\
	r12 = r13;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = r8;
	REGS_CHECK(args, "r8", v , stash32);

	stash32 = r2 + r3 + r4 + r5;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_RISCV) && (__riscv_xlen == 64)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress RISCV registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t s1  __asm__("s1")  = v;
	register uint64_t s2  __asm__("s2")  = s1 >> 1;
	register uint64_t s3  __asm__("s3")  = s1 << 1;
	register uint64_t s4  __asm__("s4")  = s1 >> 2;
	register uint64_t s5  __asm__("s5")  = s1 << 2;
	register uint64_t s6  __asm__("s6")  = ~s1;
	register uint64_t s7  __asm__("s7")  = ~s2;
	register uint64_t s8  __asm__("s8")  = ~s3;
	register uint64_t s9  __asm__("s9")  = ~s4;
	register uint64_t s10 __asm__("s10") = ~s5;
	register uint64_t s11 __asm__("s11") = s1 ^ 0xa5a5a5a5a5a5a5a5ULL;

#define SHUFFLE_REGS()	\
do {			\
	s11 = s1;	\
	s1  = s2;	\
	s2  = s3;	\
	s3  = s4;	\
	s4  = s5;	\
	s5  = s6;	\
	s6  = s7;	\
	s7  = s8;	\
	s8  = s9;	\
	s9  = s10;	\
	s10 = s11;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = s5;
	REGS_CHECK(args, "s5", v, stash64);

	stash64 = s1 + s2 + s3 + s4 + s5 + s6 +
		s7 + s8 + s9 + s10 + s11;
#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_ALPHA)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress Alpha registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t t0  __asm__("$1")  = v;
	register uint64_t t1  __asm__("$2")  = t0 >> 1;
	register uint64_t t2  __asm__("$3")  = t0 << 1;
	register uint64_t t3  __asm__("$4")  = t0 >> 2;
	register uint64_t t4  __asm__("$5")  = t0 << 2;
	register uint64_t t5  __asm__("$6")  = ~t0;
	register uint64_t t6  __asm__("$7")  = ~t1;
	register uint64_t t7  __asm__("$8")  = ~t2;
	register uint64_t t8  __asm__("$22")  = ~t3;
	register uint64_t t9  __asm__("$23")  = ~t4;
	register uint64_t t10 __asm__("$24")  = t0 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t t11 __asm__("$25")  = t1 ^ 0xa5a5a5a5a5a5a5a5ULL;

#define SHUFFLE_REGS()	\
do {			\
	t11 = t0;	\
	t0  = t1;	\
	t1  = t2;	\
	t2  = t3;	\
	t3  = t4;	\
	t4  = t5;	\
	t5  = t6;	\
	t6  = t7;	\
	t7  = t8;	\
	t8  = t9;	\
	t9  = t10;	\
	t10 = t11;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = t6;
	REGS_CHECK(args, "t6", v, stash64);

	stash64 = t0 + t1 + t2 + t3 + t5 + t6 +
		t7 + t8 + t9 + t10 + t11;
#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_PPC64)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress PPC64 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t r14 __asm__("r14") = v;
	register uint64_t r15 __asm__("r15") = r14 >> 1;
	register uint64_t r16 __asm__("r16") = r14 << 1;
	register uint64_t r17 __asm__("r17") = r14 >> 2;
	register uint64_t r18 __asm__("r18") = r14 << 2;
	register uint64_t r19 __asm__("r19") = ~r14;
	register uint64_t r20 __asm__("r20") = ~r15;
	register uint64_t r21 __asm__("r21") = ~r16;
	register uint64_t r22 __asm__("r22") = ~r17;
	register uint64_t r23 __asm__("r23") = ~r18;
	register uint64_t r24 __asm__("r24") = r14 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r25 __asm__("r25") = r15 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r26 __asm__("r26") = r16 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r27 __asm__("r27") = r17 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r28 __asm__("r28") = r18 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t r29 __asm__("r29") = r14 ^ 0x55aaaa5555aaaa55ULL;
	register uint64_t r30 __asm__("r30") = r15 ^ 0xaaaa5555aaaa5555ULL;

#define SHUFFLE_REGS()	\
do {			\
	r30 = r14;	\
	r14 = r15;	\
	r15 = r16;	\
	r16 = r17;	\
	r17 = r18;	\
	r18 = r19;	\
	r19 = r20;	\
	r20 = r21;	\
	r21 = r22;	\
	r22 = r23;	\
	r23 = r24;	\
	r24 = r25;	\
	r25 = r26;	\
	r26 = r27;	\
	r27 = r28;	\
	r28 = r29;	\
	r29 = r30;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = r14;
	REGS_CHECK(args, "r14", v, stash64);

	stash64 = r14 + r15 + r16 + r17 +
		r18 + r19 + r20 + r21 +
		r22 + r23 + r24 + r25 +
		r26 + r27 + r28 + r29 +
		r30;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_PPC)
#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress PPC registers
 *	Notice, r30 should not be used:
 *	stress-regs.c: error: r30 cannot be used in 'asm' here
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;
	register uint32_t r14 __asm__("r14") = v32;
	register uint32_t r15 __asm__("r15") = r14 >> 1;
	register uint32_t r16 __asm__("r16") = r14 << 1;
	register uint32_t r17 __asm__("r17") = r14 >> 2;
	register uint32_t r18 __asm__("r18") = r14 << 2;
	register uint32_t r19 __asm__("r19") = ~r14;
	register uint32_t r20 __asm__("r20") = ~r15;
	register uint32_t r21 __asm__("r21") = ~r16;
	register uint32_t r22 __asm__("r22") = ~r17;
	register uint32_t r23 __asm__("r23") = ~r18;
	register uint32_t r24 __asm__("r24") = r14 ^ 0xa5a5a5a5UL;
	register uint32_t r25 __asm__("r25") = r15 ^ 0xa5a5a5a5UL;
	register uint32_t r26 __asm__("r26") = r16 ^ 0xa5a5a5a5UL;
	register uint32_t r27 __asm__("r27") = r17 ^ 0xa5a5a5a5UL;
	register uint32_t r28 __asm__("r28") = r18 ^ 0xa5a5a5a5UL;
	register uint32_t r29 __asm__("r29") = r14 ^ 0xa5a5a5a5UL;

#define SHUFFLE_REGS()	\
do {			\
	r29 = r14;	\
	r14 = r15;	\
	r15 = r16;	\
	r16 = r17;	\
	r17 = r18;	\
	r18 = r19;	\
	r19 = r20;	\
	r20 = r21;	\
	r21 = r22;	\
	r22 = r23;	\
	r23 = r24;	\
	r24 = r25;	\
	r25 = r26;	\
	r26 = r27;	\
	r27 = r28;	\
	r28 = r29;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = r28;
	REGS_CHECK(args, "r28", v32, stash32);

	stash32 = r14 + r15 + r16 + r17 +
		r18 + r19 + r20 + r21 +
		r22 + r23 + r24 + r25 +
		r26 + r27 + r28 + 29;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_SPARC)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress sparc registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t l0 __asm__("l0") = v;
	register uint64_t l1 __asm__("l1") = l0 >> 1;
	register uint64_t l2 __asm__("l2") = l0 << 1;
	register uint64_t l3 __asm__("l3") = l0 >> 2;
	register uint64_t l4 __asm__("l4") = l0 << 2;
	register uint64_t l5 __asm__("l5") = ~l0;
	register uint64_t l6 __asm__("l6") = ~l1;
	register uint64_t l7 __asm__("l7") = ~l2;

#define SHUFFLE_REGS()	\
do {			\
	l7 = l0;	\
	l0 = l1;	\
	l1 = l2;	\
	l2 = l3;	\
	l3 = l4;	\
	l4 = l5;	\
	l5 = l6;	\
	l6 = l7;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = l5;
	REGS_CHECK(args, "l5", v, stash64);

	stash64 = l0 + l1 + l2 + l3 +
		l4 + l5 + l6 + l7;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_MIPS) &&	\
    defined(_MIPS_TUNE_MIPS64R2)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress MIPS64R2 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t s0 __asm__("s0") = v;
	register uint64_t s1 __asm__("s1") = s0 >> 1;
	register uint64_t s2 __asm__("s2") = s0 << 1;
	register uint64_t s3 __asm__("s3") = s0 >> 2;
	register uint64_t s4 __asm__("s4") = s0 << 2;
	register uint64_t s5 __asm__("s5") = ~s0;
	register uint64_t s6 __asm__("s6") = ~s1;
	register uint64_t s7 __asm__("s7") = ~s2;

#define SHUFFLE_REGS()	\
do {			\
	s7 = s0;	\
	s0 = s1;	\
	s1 = s2;	\
	s2 = s3;	\
	s3 = s4;	\
	s4 = s5;	\
	s5 = s6;	\
	s6 = s7;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = s5;
	REGS_CHECK(args, "s5", v, stash64);

	stash64 = s0 + s1 + s2 + s3 +
		s4 + s5 + s6 + s7;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_MIPS) &&	\
    defined(_MIPS_TUNE_MIPS32R2)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress MIPS324R2 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;

	register uint32_t s0 __asm__("s0") = v32;
	register uint32_t s1 __asm__("s1") = s0 >> 1;
	register uint32_t s2 __asm__("s2") = s0 << 1;
	register uint32_t s3 __asm__("s3") = s0 >> 2;
	register uint32_t s4 __asm__("s4") = s0 << 2;
	register uint32_t s5 __asm__("s5") = ~s0;
	register uint32_t s6 __asm__("s6") = ~s1;
	register uint32_t s7 __asm__("s7") = ~s2;

#define SHUFFLE_REGS()	\
do {			\
	s7 = s0;	\
	s0 = s1;	\
	s1 = s2;	\
	s2 = s3;	\
	s3 = s4;	\
	s4 = s5;	\
	s5 = s6;	\
	s6 = s7;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = s5;
	REGS_CHECK(args, "s0", v32, stash32);

	stash32 = s0 + s1 + s2 + s3 +
		s4 + s5 + s6 + s7;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_ARM) &&	\
    defined(__aarch64__)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress ARM64 (aarch64) registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t x0  __asm__("x0")  = v;
	register uint64_t x1  __asm__("x1")  = x0 >> 1;
	register uint64_t x2  __asm__("x2")  = x0 << 1;
	register uint64_t x3  __asm__("x3")  = x0 >> 2;
	register uint64_t x4  __asm__("x4")  = x0 << 2;
	register uint64_t x5  __asm__("x5")  = ~x0;
	register uint64_t x6  __asm__("x6")  = ~x1;
	register uint64_t x7  __asm__("x7")  = ~x2;
	register uint64_t x8  __asm__("x8")  = ~x3;
	register uint64_t x9  __asm__("x9")  = ~x4;
	register uint64_t x10 __asm__("x10") = x0 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t x11 __asm__("x11") = x1 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t x12 __asm__("x12") = x2 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t x13 __asm__("x13") = x3 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t x14 __asm__("x14") = x4 ^ 0xa5a5a5a5a5a5a5a5ULL;
	register uint64_t x15 __asm__("x15") = x0 ^ 0x5555aaaa5555aaaaULL;
	register uint64_t x16 __asm__("x16") = x1 ^ 0x55aaaa5555aaaa55ULL;
	register uint64_t x17 __asm__("x17") = x2 ^ 0xaaaa5555aaaa5555ULL;
	register uint64_t x18 __asm__("x18") = x3 ^ 0xaa5555aaaa5555aaULL;
	register uint64_t x19 __asm__("x19") = x4 ^ 0x55555555aaaaaaaaULL;
	register uint64_t x20 __asm__("x20") = x0 ^ x1;
	register uint64_t x21 __asm__("x21") = x1 ^ x2;
	register uint64_t x22 __asm__("x22") = x2 ^ x3;
	register uint64_t x23 __asm__("x23") = x3 ^ x4;
	register uint64_t x24 __asm__("x24") = x4 ^ x0;
	register uint64_t x25 __asm__("x25") = ~x20;
	register uint64_t x26 __asm__("x26") = ~x21;
	register uint64_t x27 __asm__("x27") = ~x22;
	register uint64_t x28 __asm__("x28") = ~x23;
	register uint64_t x29 __asm__("x29") = ~x24;
	register uint64_t x30 __asm__("x30") = x0 + x1;

#define SHUFFLE_REGS()	\
do {			\
	x30 = x0;	\
	x0  = x1;	\
	x1  = x2;	\
	x2  = x3;	\
	x3  = x4;	\
	x4  = x5;	\
	x5  = x6;	\
	x6  = x7;	\
	x7  = x8;	\
	x8  = x9;	\
	x9  = x10;	\
	x10 = x11;	\
	x11 = x12;	\
	x12 = x13;	\
	x13 = x14;	\
	x14 = x15;	\
	x15 = x16;	\
	x16 = x17;	\
	x17 = x18;	\
	x18 = x19;	\
	x19 = x20;	\
	x20 = x21;	\
	x21 = x22;	\
	x22 = x23;	\
	x23 = x24;	\
	x24 = x25;	\
	x25 = x26;	\
	x26 = x27;	\
	x27 = x28;	\
	x28 = x29;	\
	x29 = x30;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash64 = x14;
	REGS_CHECK(args, "x15", v, stash64);

	stash64 = x0 + x1 + x2 + x3 + x4 +
		x5 + x6 + x7 + x8 + x9 +
		x10 + x11 + x12 + x13 + x14 +
		x15 + x16 + x17 + x18 + x19 +
		x20 + x21 + x22 + x23 + x24 +
		x25 + x26 + x27 + x28 + x29 +
		x30;
#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_ARM) &&	\
    !defined(__aarch64__)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress 32 bit ARM registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;

	register uint32_t r0  __asm__("r0")  = v32;
	register uint32_t r1  __asm__("r1")  = r0 >> 1;
	register uint32_t r2  __asm__("r2")  = r0 << 1;
	register uint32_t r3  __asm__("r3")  = r0 >> 2;
	register uint32_t r4  __asm__("r4")  = r0 << 2;
	register uint32_t r5  __asm__("r5")  = ~r0;
	register uint32_t r6  __asm__("r6")  = ~r1;
	register uint32_t r8  __asm__("r8")  = ~r2;
	register uint32_t r9  __asm__("r9")  = ~r3;
	register uint32_t r10 __asm__("r10") = ~r4;

#define SHUFFLE_REGS()	\
do {			\
	r10 = r0;	\
	r0  = r1;	\
	r1  = r2;	\
	r2  = r3;	\
	r3  = r4;	\
	r4  = r5;	\
	r5  = r6;	\
	r6  = r8;	\
	r8  = r9;	\
	r9  = r10;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash32 = r2;
	REGS_CHECK(args, "r2", v32, stash32);

	stash32 = r0 + r1 + r2 + r3 + r4 +
		  r5 + r6 + r8 + r9 + r10;
#undef SHUFFLE_REGS
}
#endif

#if !defined(STRESS_REGS_HELPER)
/*
 *  stress_regs_helper(void)
 *	stress registers, generic version
 */
static void OPTIMIZE0 stress_regs_helper(stress_args_t *args, register uint64_t v)
{
	register uint64_t r1  = v;
	register uint64_t r2  = r1 >> 1;
	register uint64_t r3  = r1 << 1;
	register uint64_t r4  = r1 >> 2;
	register uint64_t r5  = r1 << 2;
	register uint64_t r6  = ~r1;
	register uint64_t r7  = ~r2;
	register uint64_t r8  = ~r3;

#define SHUFFLE_REGS()	\
do {			\
	r8 = r1;	\
	r1 = r2;	\
	r2 = r3;	\
	r3 = r4;	\
	r4 = r5;	\
	r5 = r6;	\
	r6 = r7;	\
	r7 = r8;	\
} while (0);		\

	SHUFFLE_REGS16();

	REGS_CHECK(args, "r1", v << 1, r1);
	REGS_CHECK(args, "r2", v >> 2, r2);
	REGS_CHECK(args, "r3", v << 2, r3);
	REGS_CHECK(args, "r4", ~v, r4);
	REGS_CHECK(args, "r5", ~(v >> 1), r5);
	REGS_CHECK(args, "r6", v, r6);
	REGS_CHECK(args, "r7", v >> 1, r7);
	REGS_CHECK(args, "r8", v >> 1, r7);

	stash64 = r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8;

#undef SHUFFLE_REGS
}
#endif

/*
 *  stress_regs()
 *	stress x86 syscall instruction
 */
static int stress_regs(stress_args_t *args)
{
	uint64_t v = stress_mwc64();

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_INT128_T)
	x86_cpu_flags = 0;
	x86_cpu_flags |= stress_cpu_x86_has_mmx() ? CPU_X86_MMX : 0;
	x86_cpu_flags |= stress_cpu_x86_has_sse() ? CPU_X86_SSE : 0;
#endif
	stress_regs_success = true;

	do {
		int i;

		for (i = 0; LIKELY(stress_continue_flag() & (i < 1000)); i++)
			stress_regs_helper(args, v);
		v++;
		stress_bogo_inc(args);
	} while (stress_regs_success && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return stress_regs_success ? EXIT_SUCCESS : EXIT_FAILURE;
}

const stressor_info_t stress_regs_info = {
	.stressor = stress_regs,
	.verify = VERIFY_ALWAYS,
	.classifier = CLASS_CPU,
	.help = help
};

#else

const stressor_info_t stress_regs_info = {
	.stressor = stress_unimplemented,
	.verify = VERIFY_ALWAYS,
	.classifier = CLASS_CPU,
	.help = help,
	.unimplemented_reason = "built without gcc 8 or higher supporting asm register assignments"
};
#endif
