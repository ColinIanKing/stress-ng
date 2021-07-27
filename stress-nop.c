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
	{ NULL,	"nop N",	"start N workers that burn cycles with no-ops" },
	{ NULL,	"nop-ops N",	"stop after N nop bogo no-op operations" },
	{ NULL, "nop-instr",	"speicy nop instruction to use" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_ASM_NOP)

static sigjmp_buf jmpbuf;

typedef struct {
	const char *name;
	void (*func)(const stress_args_t *args);
} stress_nop_instr_t;

#define OPx1(op)	__asm__ __volatile__(op);
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
			OPx64(op);		\
						\
		inc_counter(args);		\
	} while (keep_stressing(args));		\
}

STRESS_NOP_SPIN_OP(nop, "nop;\n")
#if defined(HAVE_ASM_PAUSE)
STRESS_NOP_SPIN_OP(pause, "pause;\n")
#endif
#if defined(HAVE_ASM_YIELD)
STRESS_NOP_SPIN_OP(yield, "yield;\n")
#endif

#if defined(STRESS_ARCH_X86)
STRESS_NOP_SPIN_OP(nop2, ".byte 0x66, 0x90\n")
STRESS_NOP_SPIN_OP(nop3, ".byte 0x0f, 0x1f, 0x00\n")
STRESS_NOP_SPIN_OP(nop4, ".byte 0x0f, 0x1f, 0x40, 0x00\n")
STRESS_NOP_SPIN_OP(nop5, ".byte 0x0f, 0x1f, 0x44, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop6, ".byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop7, ".byte 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop8, ".byte 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop9, ".byte 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop10, ".byte 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00\n")
STRESS_NOP_SPIN_OP(nop11, ".byte 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00\n")
#endif

stress_nop_instr_t nop_instr[] = {
	{ "nop",	stress_nop_spin_nop },
#if defined(STRESS_ARCH_X86)
	{ "nop2",	stress_nop_spin_nop2 },
	{ "nop3",	stress_nop_spin_nop3 },
	{ "nop4",	stress_nop_spin_nop4 },
	{ "nop5",	stress_nop_spin_nop5 },
	{ "nop6",	stress_nop_spin_nop6 },
	{ "nop7",	stress_nop_spin_nop7 },
	{ "nop8",	stress_nop_spin_nop8 },
	{ "nop9",	stress_nop_spin_nop9 },
	{ "nop10",	stress_nop_spin_nop10 },
	{ "nop11",	stress_nop_spin_nop11 },
#endif
#if defined(HAVE_ASM_PAUSE)
	{ "pause",	stress_nop_spin_pause },
#endif
#if defined(HAVE_ASM_YIELD)
	{ "yield",	stress_nop_spin_yield },
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
	.opt_set_funcs,
	.help = help
};
#endif
