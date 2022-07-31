/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-put.h"

static volatile uint64_t stash;

static const stress_help_t help[] = {
	{ NULL,	"regs N",		"start N workers exercising functions using syscall" },
	{ NULL,	"regs-ops N",	"stop after N syscall function calls" },
	{ NULL,	NULL,		NULL }
};

#if (defined(__GNUC__) && NEED_GNUC(4, 0, 0)) &&\
    !defined(__clang__)	&&	\
    !defined(__ICC) && 		\
    !defined(__PCC__) &&	\
    !defined(__TINYC__)

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

#if (defined(__x86_64__) || defined(__x86_64)) 

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress x86_64 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t rax __asm__("rax") = v;
	register uint64_t rbx __asm__("rbx") = v >> 1;
	register uint64_t rcx __asm__("rcx") = v << 1;
	register uint64_t rdx __asm__("rdx") = v >> 2;
	register uint64_t rsi __asm__("rsi") = v << 2;
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

	stash = rax + rbx + rcx + rdx +
		rsi + rdi + r8  + r9  +
		r10 + r11 + r12 + r13 +
		r14 + r15;

#undef SHUFFLE_REGS
}
#endif

#if (defined(__i386__) || defined(__i386))

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress i386 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;

	register uint64_t eax __asm__("eax") = v32;
	register uint64_t ecx __asm__("ecx") = v32 >> 1;
	register uint64_t ebx __asm__("ebx") = v32 << 1;
	register uint64_t edx __asm__("edx") = v32 >> 2;

#define SHUFFLE_REGS()	\
do {			\
	edx = eax;	\
	eax = ebx;	\
	ebx = ecx;	\
	ecx = edx;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash = eax + ebx + ecx + edx;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_M68K)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress m68000 registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	uint32_t v32 = (uint32_t)v;

	register uint64_t d1 __asm__("d1") = v32;
	register uint64_t d2 __asm__("d2") = v32 >> 1;
	register uint64_t d3 __asm__("d3") = v32 << 1;
	register uint64_t d4 __asm__("d4") = v32 >> 2;
	register uint64_t d5 __asm__("d5") = v32 << 2;
	register uint64_t d6 __asm__("d6") = v32 << 2;

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

	stash = d1 + d2 + d3 +
		d4 + d5 + d6;
#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_RISCV)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress RISCV registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t s1  __asm__("s1")  = v;
	register uint64_t s2  __asm__("s2")  = v >> 1;
	register uint64_t s3  __asm__("s3")  = v << 1;
	register uint64_t s4  __asm__("s4")  = v >> 2;
	register uint64_t s5  __asm__("s5")  = v << 2;
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

	stash = s1 + s2 + s3 + s4 + s5 + s6 +
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
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t t0  __asm__("$1")  = v;
	register uint64_t t1  __asm__("$2")  = v >> 1;
	register uint64_t t2  __asm__("$3")  = v << 1;
	register uint64_t t3  __asm__("$4")  = v >> 2;
	register uint64_t t4  __asm__("$5")  = v << 2;
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

	stash = t0 + t1 + t2 + t3 + t5 + t6 +
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
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t r14 __asm__("r14") = v;
	register uint64_t r15 __asm__("r15") = v >> 1;
	register uint64_t r16 __asm__("r16") = v << 1;
	register uint64_t r17 __asm__("r17") = v >> 2;
	register uint64_t r18 __asm__("r18") = v << 2;
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
	r28 = r29;	\
} while (0);		\

	SHUFFLE_REGS16();

	stash = r14 + r15 + r16 + r17 +
		r18 + r19 + r20 + r21 +
		r22 + r23 + r24 + r25 +
		r26 + r27 + r28 + r29 +
		r30;

#undef SHUFFLE_REGS
}
#endif

#if defined(STRESS_ARCH_SPARC)

#define STRESS_REGS_HELPER
/*
 *  stress_regs_helper(void)
 *	stress sparc registers
 */
static void NOINLINE OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t l0 __asm__("l0") = v;
	register uint64_t l1 __asm__("l1") = v >> 1;
	register uint64_t l2 __asm__("l2") = v << 1;
	register uint64_t l3 __asm__("l3") = v >> 2;
	register uint64_t l4 __asm__("l4") = v << 2;
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

	stash = l0 + l1 + l2 + l3 +
		l4 + l5 + l6 + l7;

#undef SHUFFLE_REGS
}
#endif


#if !defined(STRESS_REGS_HELPER)
/*
 *  stress_regs_helper(void)
 *	stress registers, generic version
 */
static void OPTIMIZE0 stress_regs_helper(register uint64_t v)
{
	register uint64_t r1  = v;
	register uint64_t r2  = v >> 1;
	register uint64_t r3  = v << 1;
	register uint64_t r4  = v >> 2;
	register uint64_t r5  = v << 2;
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

	stash = r1 + r2 + r3 + r4 + r5 + r6 + r7 + r8;

#undef SHUFFLE_REGS
}
#endif

/*
 *  stress_regs()
 *	stress x86 syscall instruction
 */
static int stress_regs(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	uint64_t v = stress_mwc64();

	do {
		int i;

		for (i = 0; i < 1000; i++)
			stress_regs_helper(v);
		v++;
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_regs_info = {
	.stressor = stress_regs,
	.class = CLASS_CPU,
	.help = help
};

#else

stressor_info_t stress_regs_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
