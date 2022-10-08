/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"nop N",		"start N workers that burn cycles with no-ops" },
	{ NULL,	"nop-ops N",		"stop after N nop bogo no-op operations" },
	{ NULL, "nop-instr INSTR",	"specify nop instruction to use" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_ASM_NOP)

static sigjmp_buf jmpbuf;

typedef struct {
	const char *name;
	void (*func)(const stress_args_t *args, const bool flag);
	bool ignore;
} stress_nop_instr_t;

static stress_nop_instr_t *current_instr = NULL;

#define OPx1(op)	op();
#define OPx4(op)	OPx1(op) OPx1(op) OPx1(op) OPx1(op)
#define OPx16(op)	OPx4(op) OPx4(op) OPx4(op) OPx4(op)
#define OPx64(op)	do { OPx16(op) OPx16(op) OPx16(op) OPx16(op) } while (0)

#define STRESS_NOP_SPIN_OP(name, op)		\
static void stress_nop_spin_ ## name(		\
	const stress_args_t *args,		\
	const bool flag)			\
{						\
	do {					\
		register int i = 1024;		\
						\
		while (i--)			\
			OPx64(op); 		\
						\
		inc_counter(args);		\
	} while (flag && keep_stressing(args));	\
}

static inline void stress_op_nop(void)
{
#if defined(STRESS_ARCH_KVX)
	/*
	 * Extra ;; required for KVX to indicate end of
	 * a VLIW instruction bundle
	 */
	__asm__ __volatile__("nop\n;;\n");
#else
	__asm__ __volatile__("nop;\n");
#endif
}

STRESS_NOP_SPIN_OP(nop, stress_op_nop)

#if defined(HAVE_ASM_X86_PAUSE)
static inline void stress_op_x86_pause(void)
{
	__asm__ __volatile__("pause;\n" ::: "memory");
}

STRESS_NOP_SPIN_OP(x86_pause, stress_op_x86_pause)
#endif

#if defined(HAVE_ASM_X86_TPAUSE)
static void x86_tpause(uint32_t ecx, uint32_t delay)
{
	uint32_t lo, hi;
	uint64_t val;

	__asm__ __volatile__("rdtsc\n" : "=a"(lo),"=d"(hi));
	val = (((uint64_t)hi << 32) | lo) + delay;
	lo = val & 0xffffffff;
	hi = val >> 32;
	__asm__ __volatile__("tpause %%ecx\n" :: "c"(ecx), "d"(hi), "a"(lo));
}

static void stress_op_x86_tpause(void)
{
	x86_tpause(0, 5000);
	x86_tpause(1, 5000);
}

STRESS_NOP_SPIN_OP(x86_tpause, stress_op_x86_tpause)
#endif

#if defined(HAVE_ASM_ARM_YIELD)
static inline void stress_op_arm_yield(void)
{
	__asm__ __volatile__("yield;\n");
}

STRESS_NOP_SPIN_OP(arm_yield, stress_op_arm_yield);
#endif

#if defined(STRESS_ARCH_X86)
static inline void stress_op_x86_nop2(void)
{
	__asm__ __volatile__(".byte 0x66, 0x90;\n");
}

static inline void stress_op_x86_nop3(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x00;\n");
}

static inline void stress_op_x86_nop4(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x40, 0x00;\n");
}

static inline void stress_op_x86_nop5(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x44, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop6(void)
{
	__asm__ __volatile__(".byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop7(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop8(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop9(void)
{
	__asm__ __volatile__(".byte 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop10(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline void stress_op_x86_nop11(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

STRESS_NOP_SPIN_OP(x86_nop2, stress_op_x86_nop2)
STRESS_NOP_SPIN_OP(x86_nop3, stress_op_x86_nop3)
STRESS_NOP_SPIN_OP(x86_nop4, stress_op_x86_nop4)
STRESS_NOP_SPIN_OP(x86_nop5, stress_op_x86_nop5)
STRESS_NOP_SPIN_OP(x86_nop6, stress_op_x86_nop6)
STRESS_NOP_SPIN_OP(x86_nop7, stress_op_x86_nop7)
STRESS_NOP_SPIN_OP(x86_nop8, stress_op_x86_nop8)
STRESS_NOP_SPIN_OP(x86_nop9, stress_op_x86_nop9)
STRESS_NOP_SPIN_OP(x86_nop10, stress_op_x86_nop10)
STRESS_NOP_SPIN_OP(x86_nop11, stress_op_x86_nop11)
#endif

#if defined(STRESS_ARCH_PPC64)
static inline void stress_op_ppc64_yield(void)
{
	__asm__ __volatile__("or 27,27,27;\n");
}

static inline void stress_op_ppc64_mdoio(void)
{
	__asm__ __volatile__("or 29,29,29;\n");
}

static inline void stress_op_ppc64_mdoom(void)
{
	__asm__ __volatile__("or 30,30,30;\n");
}

STRESS_NOP_SPIN_OP(ppc64_yield, stress_op_ppc64_yield);
STRESS_NOP_SPIN_OP(ppc64_mdoio, stress_op_ppc64_mdoio);
STRESS_NOP_SPIN_OP(ppc64_mdoom, stress_op_ppc64_mdoom);
#endif

#if defined(STRESS_ARCH_S390)
static inline void stress_op_s390_nopr(void)
{
	__asm__ __volatile__("nopr %r0;\n");
}

STRESS_NOP_SPIN_OP(s390_nopr, stress_op_s390_nopr);
#endif

static void stress_nop_random(const stress_args_t *args, const bool flag);

static stress_nop_instr_t nop_instr[] = {
	{ "nop",	stress_nop_spin_nop,		false },
#if defined(STRESS_ARCH_X86)
	{ "nop2",	stress_nop_spin_x86_nop2,	false },
	{ "nop3",	stress_nop_spin_x86_nop3,	false },
	{ "nop4",	stress_nop_spin_x86_nop4,	false },
	{ "nop5",	stress_nop_spin_x86_nop5,	false },
	{ "nop6",	stress_nop_spin_x86_nop6,	false },
	{ "nop7",	stress_nop_spin_x86_nop7,	false },
	{ "nop8",	stress_nop_spin_x86_nop8,	false },
	{ "nop9",	stress_nop_spin_x86_nop9,	false },
	{ "nop10",	stress_nop_spin_x86_nop10,	false },
	{ "nop11",	stress_nop_spin_x86_nop11,	false },
#endif
#if defined(STRESS_ARCH_S390)
	{ "nopr",	stress_nop_spin_s390_nopr,	false },
#endif
#if defined(HAVE_ASM_X86_PAUSE)
	{ "pause",	stress_nop_spin_x86_pause,	false },
#endif
#if defined(HAVE_ASM_X86_TPAUSE)
	{ "tpause",	stress_nop_spin_x86_tpause,	false },
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	{ "yield",	stress_nop_spin_arm_yield,	false },
#endif
#if defined(STRESS_ARCH_PPC64)
	{ "mdoio",	stress_nop_spin_ppc64_mdoio,	false },
	{ "mdoom",	stress_nop_spin_ppc64_mdoom,	false },
	{ "yield",	stress_nop_spin_ppc64_yield,	false },
#endif
	{ "random",	stress_nop_random,		false },
	{ NULL,		NULL ,				false },
};

static inline void stress_nop_callfunc(
	const stress_nop_instr_t *instr,
	const stress_args_t *args,
	const bool flag)
{
	if (UNLIKELY(instr->ignore))
		stress_nop_spin_nop(args, flag);
	else
		instr->func(args, flag);
}

static void stress_nop_random(const stress_args_t *args, const bool flag)
{
	(void)flag;

	do {
		const size_t n = stress_mwc8() % (SIZEOF_ARRAY(nop_instr) - 2);

		current_instr = &nop_instr[n];
		if (!current_instr->ignore)
			stress_nop_callfunc(current_instr, args, false);
	} while (keep_stressing(args));
}

static int stress_set_nop_instr(const char *opt)
{
	stress_nop_instr_t *instr;

	for (instr = nop_instr; instr->func; instr++) {
		current_instr = instr;
		if (!strcmp(instr->name, opt)) {
			stress_set_setting("nop-instr", TYPE_ID_UINTPTR_T, &instr);
			return 0;
		}
	}

	(void)fprintf(stderr, "nop-instr must be one of:");
	for (instr = nop_instr; instr->func; instr++) {
		(void)fprintf(stderr, " %s", instr->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static void NORETURN stress_sigill_nop_handler(int signum)
{
	(void)signum;

	current_instr->ignore = true;

	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_nop()
 *	stress that does lots of not a lot
 */
static int stress_nop(const stress_args_t *args)
{
	stress_nop_instr_t *instr = &nop_instr[0];
	bool do_random;

	(void)stress_get_setting("nop-instr", &instr);

	if (stress_sighandler(args->name, SIGILL, stress_sigill_nop_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do_random = (instr->func == stress_nop_random);

	if (sigsetjmp(jmpbuf, 1) != 0) {
		/* We reach here on an SIGILL trap */
		if (current_instr == &nop_instr[0]) {
			/* Really should be able to do nop, skip */
			pr_inf_skip("%s 'nop' instruction was illegal, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		} else {
			/* not random choice?, then default to nop */
			if (!do_random)
				instr = &nop_instr[0];
			pr_inf("%s '%s' instruction was illegal, ignoring, defaulting to nop\n",
				args->name, current_instr->name);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	current_instr = instr;
	stress_nop_callfunc(instr, args, true);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_nop_instr,	stress_set_nop_instr },
	{ 0,                    NULL }
};

stressor_info_t stress_nop_info = {
	.stressor = stress_nop,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_nop_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
