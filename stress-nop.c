/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King.
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
#include "core-asm-arm.h"
#include "core-asm-ppc64.h"
#include "core-asm-x86.h"
#include "core-cpu.h"

#define NOP_LOOPS	(1024)

static const stress_help_t help[] = {
	{ NULL,	"nop N",		"start N workers that burn cycles with no-ops" },
	{ NULL, "nop-instr INSTR",	"specify nop instruction to use" },
	{ NULL,	"nop-ops N",		"stop after N nop bogo no-op operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_ASM_NOP) &&	\
    defined(HAVE_SIGLONGJMP)

static sigjmp_buf jmpbuf;

typedef void (*nop_func_t)(stress_args_t *args,
			   const bool flag,
			   double *duration,
			   double *count);

typedef struct {
	const char *name;
	const nop_func_t nop_func;
	bool (*supported)(void);
	bool ignore;
	bool supported_check;
} stress_nop_instr_t;

static stress_nop_instr_t *current_instr = NULL;

#define OPx1(op)	op();
#define OPx4(op)	OPx1(op) OPx1(op) OPx1(op) OPx1(op)
#define OPx16(op)	OPx4(op) OPx4(op) OPx4(op) OPx4(op)
#define OPx64(op)	do { OPx16(op) OPx16(op) OPx16(op) OPx16(op) } while (0)

#define STRESS_NOP_SPIN_OP(name, op)				\
static void stress_nop_spin_ ## name(				\
	stress_args_t *args,					\
	const bool flag,					\
	double *duration,					\
	double *count)						\
{								\
	do {							\
		register int j = 64;				\
								\
		while (LIKELY(j--)) {				\
			register int i = NOP_LOOPS;		\
			const double t = stress_time_now();	\
								\
			while (LIKELY(i--))			\
				OPx64(op); 			\
			(*duration) += stress_time_now() - t;	\
			(*count) += (double)(64 * NOP_LOOPS);	\
								\
			stress_bogo_inc(args);			\
		}						\
	} while (flag && stress_continue(args));		\
}

STRESS_NOP_SPIN_OP(nop, stress_asm_nop)

#if defined(HAVE_ASM_X86_PAUSE)
STRESS_NOP_SPIN_OP(x86_pause, stress_asm_x86_pause)
#endif

#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
static inline ALWAYS_INLINE void stress_op_x86_tpause(void)
{
	uint64_t tsc;

	tsc = stress_asm_x86_rdtsc();
	stress_asm_x86_tpause(0, 10000 + tsc);
	tsc = stress_asm_x86_rdtsc();
	stress_asm_x86_tpause(1, 10000 + tsc);
}

STRESS_NOP_SPIN_OP(x86_tpause, stress_op_x86_tpause)
#endif

#if defined(HAVE_ASM_X86_SERIALIZE)
STRESS_NOP_SPIN_OP(x86_serialize, stress_asm_x86_serialize)
#endif

#if defined(HAVE_ASM_ARM_YIELD)
STRESS_NOP_SPIN_OP(arm_yield, stress_asm_arm_yield);
#endif

#if defined(STRESS_ARCH_X86)
static inline ALWAYS_INLINE void stress_op_x86_nop2(void)
{
	__asm__ __volatile__(".byte 0x66, 0x90;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop3(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop4(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x40, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop5(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x44, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop6(void)
{
	__asm__ __volatile__(".byte 0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop7(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop8(void)
{
	__asm__ __volatile__(".byte 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop9(void)
{
	__asm__ __volatile__(".byte 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop10(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop11(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop12(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop13(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop14(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_nop15(void)
{
	__asm__ __volatile__(".byte 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00;\n");
}

static inline ALWAYS_INLINE void stress_op_x86_fnop(void)
{
	__asm__ __volatile__(".byte 0xd9, 0xd0;\n");
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
STRESS_NOP_SPIN_OP(x86_nop12, stress_op_x86_nop12)
STRESS_NOP_SPIN_OP(x86_nop13, stress_op_x86_nop13)
STRESS_NOP_SPIN_OP(x86_nop14, stress_op_x86_nop14)
STRESS_NOP_SPIN_OP(x86_nop15, stress_op_x86_nop15)
STRESS_NOP_SPIN_OP(x86_fnop, stress_op_x86_fnop)
#endif

#if defined(STRESS_ARCH_PPC64)
STRESS_NOP_SPIN_OP(ppc64_yield, stress_asm_ppc64_yield);
STRESS_NOP_SPIN_OP(ppc64_mdoio, stress_asm_ppc64_mdoio);
STRESS_NOP_SPIN_OP(ppc64_mdoom, stress_asm_ppc64_mdoom);
#endif

#if defined(STRESS_ARCH_PPC)
STRESS_NOP_SPIN_OP(ppc_yield, stress_asm_ppc_yield);
STRESS_NOP_SPIN_OP(ppc_mdoio, stress_asm_ppc_mdoio);
STRESS_NOP_SPIN_OP(ppc_mdoom, stress_asm_ppc_mdoom);
#endif

#if defined(STRESS_ARCH_S390)
static inline void stress_op_s390_nopr(void)
{
	__asm__ __volatile__("nopr %r0;\n");
}

STRESS_NOP_SPIN_OP(s390_nopr, stress_op_s390_nopr);
#endif

static void stress_nop_random(stress_args_t *args, const bool flag,
			      double *duration, double *count);

static stress_nop_instr_t nop_instrs[] = {
	{ "nop",	stress_nop_spin_nop,		NULL,	false,	false },
#if defined(STRESS_ARCH_X86)
	{ "nop2",	stress_nop_spin_x86_nop2,	NULL,	false,	false },
	{ "nop3",	stress_nop_spin_x86_nop3,	NULL,	false,	false },
	{ "nop4",	stress_nop_spin_x86_nop4,	NULL,	false,	false },
	{ "nop5",	stress_nop_spin_x86_nop5,	NULL,	false,	false },
	{ "nop6",	stress_nop_spin_x86_nop6,	NULL,	false,	false },
	{ "nop7",	stress_nop_spin_x86_nop7,	NULL,	false,	false },
	{ "nop8",	stress_nop_spin_x86_nop8,	NULL,	false,	false },
	{ "nop9",	stress_nop_spin_x86_nop9,	NULL,	false,	false },
	{ "nop10",	stress_nop_spin_x86_nop10,	NULL,	false,	false },
	{ "nop11",	stress_nop_spin_x86_nop11,	NULL,	false,	false },
	{ "nop12",	stress_nop_spin_x86_nop12,	NULL,	false,	false },
	{ "nop13",	stress_nop_spin_x86_nop13,	NULL,	false,	false },
	{ "nop14",	stress_nop_spin_x86_nop14,	NULL,	false,	false },
	{ "nop15",	stress_nop_spin_x86_nop15,	NULL,	false,	false },
	{ "fnop",	stress_nop_spin_x86_fnop,	NULL,	false,	false },
#endif
#if defined(STRESS_ARCH_S390)
	{ "nopr",	stress_nop_spin_s390_nopr,	NULL,	false,	false },
#endif
#if defined(HAVE_ASM_X86_PAUSE)
	{ "pause",	stress_nop_spin_x86_pause,	NULL,	false,	false },
#endif
#if defined(HAVE_ASM_X86_SERIALIZE)
	{ "serialize",	stress_nop_spin_x86_serialize,	stress_cpu_x86_has_serialize,	false,	false },
#endif
#if defined(HAVE_ASM_X86_TPAUSE) &&	\
    !defined(HAVE_COMPILER_PCC)
	{ "tpause",	stress_nop_spin_x86_tpause,	stress_cpu_x86_has_waitpkg,	false,	false },
#endif
#if defined(HAVE_ASM_ARM_YIELD)
	{ "yield",	stress_nop_spin_arm_yield,	NULL,	false,	false },
#endif
#if defined(STRESS_ARCH_PPC64)
	{ "mdoio",	stress_nop_spin_ppc64_mdoio,	NULL,	false,	false },
	{ "mdoom",	stress_nop_spin_ppc64_mdoom,	NULL,	false,	false },
	{ "yield",	stress_nop_spin_ppc64_yield,	NULL,	false,	false },
#endif
#if defined(STRESS_ARCH_PPC)
	{ "mdoio",	stress_nop_spin_ppc_mdoio,	NULL,	false,	false },
	{ "mdoom",	stress_nop_spin_ppc_mdoom,	NULL,	false,	false },
	{ "yield",	stress_nop_spin_ppc_yield,	NULL,	false,	false },
#endif
	/* Must be last of the array */
	{ "random",	stress_nop_random,		NULL,	false,	false },
};

static inline void stress_nop_callfunc(
	stress_nop_instr_t *instr,
	stress_args_t *args,
	const bool flag,
	double *duration,
	double *count)
{
	if (UNLIKELY(instr->supported_check == false)) {
		instr->supported_check = true;
		if (instr->supported && !instr->supported()) {
			if (stress_instance_zero(args))
				pr_inf("%s: '%s' instruction is not supported, ignoring, defaulting to nop\n",
					args->name, instr->name);
			instr->ignore = true;
		}
	}
	if (UNLIKELY(instr->ignore))
		stress_nop_spin_nop(args, flag, duration, count);
	else
		instr->nop_func(args, flag, duration, count);
}

static void stress_nop_random(
	stress_args_t *args,
	const bool flag,
	double *duration,
	double *count)
{
	(void)flag;

	do {
		/* the -1 stops us from calling random recursively */
		const size_t n = stress_mwc8modn(SIZEOF_ARRAY(nop_instrs) - 1);

		current_instr = &nop_instrs[n];
		stress_nop_callfunc(current_instr, args, false, duration, count);
	} while (stress_continue(args));
}

static void NORETURN stress_sigill_nop_handler(int signum)
{
	(void)signum;

	current_instr->ignore = true;

	siglongjmp(jmpbuf, 1);
	stress_no_return();
}

/*
 *  stress_nop()
 *	stress that does lots of not a lot
 */
static int stress_nop(stress_args_t *args)
{
	size_t nop_instr = 0;
	NOCLOBBER stress_nop_instr_t *instr;
	NOCLOBBER bool do_random;
	double duration = 0.0, count = 0.0, rate;

	(void)stress_get_setting("nop-instr", &nop_instr);
	instr = &nop_instrs[nop_instr];

	if (stress_sighandler(args->name, SIGILL, stress_sigill_nop_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	do_random = (instr->nop_func == stress_nop_random);

	if (sigsetjmp(jmpbuf, 1) != 0) {
		/* We reach here on an SIGILL trap */
		if (current_instr == &nop_instrs[0]) {
			/* Really should be able to do nop, skip */
			pr_inf_skip("%s: 'nop' instruction was illegal, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		} else {
			/* not random choice?, then default to nop */
			if (!do_random)
				instr = &nop_instrs[0];
			pr_inf("%s: '%s' instruction was illegal, ignoring, defaulting to nop\n",
				args->name, current_instr->name);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	current_instr = instr;
	stress_nop_callfunc(instr, args, true, &duration, &count);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "picosecs per nop instruction",
		STRESS_DBL_NANOSECOND * rate, STRESS_METRIC_HARMONIC_MEAN);

	return EXIT_SUCCESS;
}

static const char *stress_nop_instr(const size_t i)
{
	return (i < SIZEOF_ARRAY(nop_instrs)) ? nop_instrs[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_nop_instr, "nop-instr", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_nop_instr },
	END_OPT,
};

const stressor_info_t stress_nop_info = {
	.stressor = stress_nop,
	.classifier = CLASS_CPU,
	.opts = opts,
	.help = help
};
#else

static const stress_opt_t opts[] = {
	{ OPT_nop_instr, "nop-instr", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

const stressor_info_t stress_nop_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "no nop assembler op-code(s) for this architecture or no support for siglongjmp"
};
#endif
