/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

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
	void (*func)(const stress_args_t *args);
} stress_nop_instr_t;

#define OPx1(op)	op();
#define OPx4(op)	OPx1(op) OPx1(op) OPx1(op) OPx1(op)
#define OPx16(op)	OPx4(op) OPx4(op) OPx4(op) OPx4(op)
#define OPx64(op)	do { OPx16(op) OPx16(op) OPx16(op) OPx16(op) } while (0)

#define STRESS_NOP_SPIN_OP(name, op)		\
static void stress_nop_spin_ ## name(const stress_args_t *args)	\
{						\
	do {					\
		register int i = 1024;		\
						\
		while (i--)			\
			OPx64(op); 		\
						\
		inc_counter(args);		\
	} while (keep_stressing(args));		\
}

static inline void stress_op_nop(void)
{
#if defined(STRESS_ARCH_KVX)
	/*
	 * Extra ;; required for KVX to indicate end of
	 * a VLIW instruction bundle
	 */
	__asm__ __volatile__("nop;;\n");
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

STRESS_NOP_SPIN_OP(x86_pause, stress_op_x86_pause);
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

STRESS_NOP_SPIN_OP(x86_nop2, stress_op_x86_nop2);
STRESS_NOP_SPIN_OP(x86_nop3, stress_op_x86_nop3);
STRESS_NOP_SPIN_OP(x86_nop4, stress_op_x86_nop4);
STRESS_NOP_SPIN_OP(x86_nop5, stress_op_x86_nop5);
STRESS_NOP_SPIN_OP(x86_nop6, stress_op_x86_nop6);
STRESS_NOP_SPIN_OP(x86_nop7, stress_op_x86_nop7);
STRESS_NOP_SPIN_OP(x86_nop8, stress_op_x86_nop8);
STRESS_NOP_SPIN_OP(x86_nop9, stress_op_x86_nop9);
STRESS_NOP_SPIN_OP(x86_nop10, stress_op_x86_nop10);
STRESS_NOP_SPIN_OP(x86_nop11, stress_op_x86_nop11);
#endif

stress_nop_instr_t nop_instr[] = {
	{ "nop",	stress_nop_spin_nop },
#if defined(STRESS_ARCH_X86)
	{ "nop2",	stress_nop_spin_x86_nop2 },
	{ "nop3",	stress_nop_spin_x86_nop3 },
	{ "nop4",	stress_nop_spin_x86_nop4 },
	{ "nop5",	stress_nop_spin_x86_nop5 },
	{ "nop6",	stress_nop_spin_x86_nop6 },
	{ "nop7",	stress_nop_spin_x86_nop7 },
	{ "nop8",	stress_nop_spin_x86_nop8 },
	{ "nop9",	stress_nop_spin_x86_nop9 },
	{ "nop10",	stress_nop_spin_x86_nop10 },
	{ "nop11",	stress_nop_spin_x86_nop11 },
#endif
#if defined(HAVE_ASM_X86_PAUSE)
	{ "pause",	stress_nop_spin_x86_pause },
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	{ "yield",	stress_nop_spin_arm_yield },
#endif
	{ NULL,		NULL },
};

static int stress_set_nop_instr(const char *opt)
{
	stress_nop_instr_t const *instr;

	for (instr = nop_instr; instr->func; instr++) {
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

static void stress_sigill_nop_handler(int signum)
{
	(void)signum;

	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_nop()
 *	stress that does lots of not a lot
 */
static int stress_nop(const stress_args_t *args)
{
	stress_nop_instr_t const *instr = &nop_instr[0];

	(void)stress_get_setting("nop-instr", &instr);

	if (stress_sighandler(args->name, SIGILL, stress_sigill_nop_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	if (sigsetjmp(jmpbuf, 1) != 0) {
		/* We reach here on an SIGILL trap */
		if (instr != &nop_instr[0]) {
			pr_inf("%s '%s' instruction was illegal, falling back to nop\n",
				args->name, instr->name);
			instr = &nop_instr[0];
		} else {
			/* Really should be able to do nop, skip */
			pr_inf("%s 'nop' instruction was illegal, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	instr->func(args);
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
