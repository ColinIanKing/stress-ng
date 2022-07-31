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
