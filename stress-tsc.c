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
#include "core-asm-loong64.h"
#include "core-asm-riscv.h"
#include "core-asm-s390.h"
#include "core-asm-sparc.h"
#include "core-asm-x86.h"
#include "core-cpu.h"

#if defined(HAVE_SYS_PLATFORM_PPC_H)
#include <sys/platform/ppc.h>
#endif

#if defined(HAVE_SYS_CAPABILITY_H)
#include <sys/capability.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"tsc N",	"start N workers reading the time stamp counter" },
	{ NULL,	"tsc-ops N",	"stop after N TSC bogo operations" },
	{ NULL, "tsc-lfence",	"add lfence after TSC reads for serialization (x86 only)" },
	{ NULL,	"tsc-rdscp",	"use rdtscp instead of rdtsc, disables tsc-lfence (x86 only)" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_tsc_lfence, "tsc-lfence", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_tsc_rdtscp, "tsc-rdtscp", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(STRESS_ARCH_LOONG64) &&	\
    defined(HAVE_ASM_LOONG64_RDTIME)

#define HAVE_STRESS_TSC_CAPABILITY

static bool tsc_supported = true;

static int stress_tsc_supported(const char *name)
{
	(void)name;

	return 0;
}

static inline uint64_t rdtsc(void) {
	return stress_asm_loong64_rdtime();
}

#endif

#if defined(STRESS_ARCH_RISCV) &&	\
    defined(SIGILL)

#define HAVE_STRESS_TSC_CAPABILITY

static sigjmp_buf jmpbuf;
static bool tsc_supported = false;

static inline uint64_t rdtsc(void)
{
	return stress_asm_riscv_rdtime();
}

static void stress_sigill_handler(int signum)
{
	(void)signum;

	siglongjmp(jmpbuf, 1);
	stress_no_return();
}

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, riscv variant
 */
static int stress_tsc_supported(const char *name)
{
	unsigned long int cycles;

	if (stress_sighandler(name, SIGILL, stress_sigill_handler, NULL) < 0)
		return -1;

	/*
	 *  We get here with non-zero return if SIGILL occurs
	 */
	if (sigsetjmp(jmpbuf, 1) != 0) {
		pr_inf_skip("%s stressor will be skipped, "
			"rdtime not allowed\n", name);
		return -1;
	}

	cycles = rdtsc();
	(void)cycles;
	tsc_supported = true;

	return 0;
}
#endif

#if defined(STRESS_ARCH_X86) &&		\
    !defined(HAVE_COMPILER_PCC) &&	\
    !defined(HAVE_COMPILER_TCC)

#define HAVE_STRESS_TSC_CAPABILITY

#if defined(HAVE_ASM_X86_LFENCE)
#define HAVE_STRESS_TSC_LFENCE
static inline void lfence(void)
{
	stress_asm_x86_lfence();
}
#endif

static bool tsc_supported = false;

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, x86 variant
 */
static int stress_tsc_supported(const char *name)
{
	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf_skip("%s stressor will be skipped, "
			"not a recognised Intel CPU\n", name);
		return -1;
	}
	/* ..and supports tsc? */
	if (!stress_cpu_x86_has_tsc()) {
		pr_inf_skip("%s stressor will be skipped, CPU "
			"does not support the tsc instruction\n", name);
		return -1;
	}
	tsc_supported = true;
	return 0;
}

/*
 *  x86 read TSC
 */
static inline uint64_t rdtsc(void)
{
	return stress_asm_x86_rdtsc();
}

#elif (defined(STRESS_ARCH_PPC64) || defined(STRESS_ARCH_PPC)) &&	\
      defined(HAVE_SYS_PLATFORM_PPC_H) &&	\
      defined(HAVE_PPC_GET_TIMEBASE)

#define HAVE_STRESS_TSC_CAPABILITY

static bool tsc_supported = true;

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, ppc variant
 */
static int stress_tsc_supported(const char *name)
{
	(void)name;

	return 0;
}

static inline uint64_t rdtsc(void)
{
	return (uint64_t)__ppc_get_timebase();
}

#elif defined(STRESS_ARCH_S390)

#define HAVE_STRESS_TSC_CAPABILITY

static bool tsc_supported = true;

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, s390x variant
 */
static int stress_tsc_supported(const char *name)
{
	(void)name;

	return 0;
}

static inline uint64_t rdtsc(void)
{
	return stress_asm_s390_stck();
}

#elif defined(STRESS_ARCH_SPARC) &&	\
      defined(HAVE_ASM_SPARC_TICK)

#define HAVE_STRESS_TSC_CAPABILITY

static bool tsc_supported = true;

static int stress_tsc_supported(const char *name)
{
	(void)name;

	return 0;
}

static inline uint64_t rdtsc(void)
{
	return stress_asm_sparc_tick();
}

#endif

#if defined(HAVE_STRESS_TSC_CAPABILITY)
static void stress_tsc_check(
	stress_args_t *args,
	const uint64_t tsc,
	const uint64_t old_tsc,
	int *ret)
{
	if (LIKELY(tsc > old_tsc))
		return;

	/* top bits different, tsc -> wrapped around? */
	if (((old_tsc ^ tsc) >> 63) == 1)
		return;

	pr_fail("%s: TSC not monitonically increasing, TSC %" PRIx64 " vs previous TSC %" PRIx64 "\n",
		args->name, tsc, old_tsc);
	*ret = EXIT_FAILURE;
	return;
}
#endif

#if defined(HAVE_STRESS_TSC_CAPABILITY)
/*
 *  Unrolled 32 times, no verify
 */
#define TSCx32()	\
do {			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
} while (0)

/*
 *  Unrolled 32 times, verify monitonically increasing at end
 */
#define TSCx32_verify(args, tsc, old_tsc, ret)	\
do {			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	tsc = rdtsc();	\
	stress_tsc_check(args, tsc, old_tsc, &ret);	\
	old_tsc = tsc;	\
} while (0)

#if defined(HAVE_STRESS_TSC_CAPABILITY) &&	\
    defined(HAVE_ASM_X86_RDTSCP)
/*
 *  Unrolled 32 times, no verify
 */
#define TSCPx32()			\
do {					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
} while (0)

/*
 *  Unrolled 32 times, verify monitonically increasing at end
 */
#define TSCPx32_verify(args, tsc, old_tsc, ret)	\
do {					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
					\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	stress_asm_x86_rdtscp();	\
	tsc = stress_asm_x86_rdtscp();	\
	stress_tsc_check(args, tsc, old_tsc, &ret);	\
	old_tsc = tsc;	\
} while (0)
#endif

#if defined(HAVE_STRESS_TSC_LFENCE)

#define rdtsc_lfence()		\
	do {			\
		rdtsc();	\
		lfence();	\
	} while (0);

/*
 *  Unrolled 32 times, no verify with lfence
 */
#define TSCx32_lfence()	\
do {			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
} while (0)

/*
 *  Unrolled 32 times, verify monitonically increasing at end, with lfence
 */
#define TSCx32_lfence_verify(args, tsc, old_tsc, ret)	\
do {			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
			\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	rdtsc_lfence();	\
	tsc = rdtsc();	\
	stress_tsc_check(args, tsc, old_tsc, &ret);	\
	old_tsc = tsc;	\
} while (0)

#endif

#if defined(HAVE_STRESS_TSC_LFENCE)
static int stress_tsc_lfence(stress_args_t *args, const bool verify, double *duration)
{
	int ret = EXIT_SUCCESS;

	if (verify) {
		uint64_t tsc, old_tsc;

		old_tsc = rdtsc();
		do {
			const double t = stress_time_now();

			TSCx32_lfence_verify(args, tsc, old_tsc, ret);
			TSCx32_lfence_verify(args, tsc, old_tsc, ret);
			TSCx32_lfence_verify(args, tsc, old_tsc, ret);
			TSCx32_lfence_verify(args, tsc, old_tsc, ret);

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	} else {
		do {
			const double t = stress_time_now();

			TSCx32_lfence();
			TSCx32_lfence();
			TSCx32_lfence();
			TSCx32_lfence();

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	}
	return ret;
}
#endif

#if defined(HAVE_STRESS_TSC_CAPABILITY) &&	\
    defined(HAVE_ASM_X86_RDTSCP)
static int stress_tsc_rdtscp(stress_args_t *args, const bool verify, double *duration)
{
	int ret = EXIT_SUCCESS;

	if (verify) {
		uint64_t tsc, old_tsc;

		old_tsc = rdtsc();
		do {
			const double t = stress_time_now();

			TSCPx32_verify(args, tsc, old_tsc, ret);
			TSCPx32_verify(args, tsc, old_tsc, ret);
			TSCPx32_verify(args, tsc, old_tsc, ret);
			TSCPx32_verify(args, tsc, old_tsc, ret);

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	} else {
		do {
			const double t = stress_time_now();

			TSCPx32();
			TSCPx32();
			TSCPx32();
			TSCPx32();

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	}
	return ret;
}
#endif

static int stress_tsc_generic(stress_args_t *args, const bool verify, double *duration)
{
	int ret = EXIT_SUCCESS;

	if (verify) {
		uint64_t tsc, old_tsc;

		old_tsc = rdtsc();
		do {
			const double t = stress_time_now();

			TSCx32_verify(args, tsc, old_tsc, ret);
			TSCx32_verify(args, tsc, old_tsc, ret);
			TSCx32_verify(args, tsc, old_tsc, ret);
			TSCx32_verify(args, tsc, old_tsc, ret);

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	} else {
		do {
			const double t = stress_time_now();

			TSCx32();
			TSCx32();
			TSCx32();
			TSCx32();

			(*duration) += stress_time_now() - t;
			stress_bogo_inc(args);
		} while (stress_continue(args));
	}
	return ret;
}

/*
 *  stress_tsc()
 *      stress Intel tsc instruction
 */
static int stress_tsc(stress_args_t *args)
{
	bool tsc_lfence = false;
	bool tsc_rdtscp = false;
	int ret = EXIT_SUCCESS;
	int (*tsc_func)(stress_args_t *args, const bool verify, double *duration) = stress_tsc_generic;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)stress_get_setting("tsc-lfence", &tsc_lfence);
	(void)stress_get_setting("tsc-rdtscp", &tsc_rdtscp);

	if (tsc_lfence) {
#if defined(HAVE_STRESS_TSC_LFENCE)
		if (!stress_cpu_is_x86()) {
			pr_inf("%s: tsc-lfence is disabled, this is an x86 only option\n", args->name);
			tsc_lfence = false;
		} else {
			tsc_func = stress_tsc_lfence;
		}
#else
		pr_inf("%s: tsc-lfence is disabled, not supported by the compiler\n", args->name);
		tsc_lfence = false;
#endif
	}
	if (tsc_rdtscp) {
#if defined(HAVE_STRESS_TSC_CAPABILITY) &&	\
    defined(HAVE_ASM_X86_RDTSCP)
		if (!stress_cpu_is_x86()) {
			pr_inf("%s: tsc-rdtscp is disabled, this is an x86 only option\n", args->name);
			tsc_rdtscp = false;
		}
		if (tsc_rdtscp && !stress_cpu_x86_has_rdtscp()) {
			pr_inf("%s: tsc-rdtscp is disabled, not supported by this x86\n", args->name);
			tsc_rdtscp = false;
		}
		if (tsc_rdtscp && tsc_lfence) {
			pr_inf("%s: tsc-rdtscp disables tsc-lfence option\n", args->name);
			tsc_lfence = false;
		}
		if (tsc_rdtscp)
			tsc_func = stress_tsc_rdtscp;
#else
		pr_inf("%s: tsc-rdtscp is disabled, not supported by the compiler\n", args->name);
		tsc_rdtscp = false;
#endif
	}

	if (tsc_supported) {
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
		double duration = 0.0, count;

		tsc_func(args, verify, &duration);
		count = 32.0 * 4.0 * (double)stress_bogo_get(args);
		duration = (count > 0.0) ? duration / count : 0.0;
		stress_metrics_set(args, 0, "nanosecs per time counter read",
			duration * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

const stressor_info_t stress_tsc_info = {
	.stressor = stress_tsc,
	.supported = stress_tsc_supported,
	.classifier = CLASS_CPU,
	.verify = VERIFY_OPTIONAL,
	.opts = opts,
	.help = help
};
#else

static int stress_tsc_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, CPU "
		"does not support the rdtsc instruction.\n", name);
	return -1;
}

const stressor_info_t stress_tsc_info = {
	.stressor = stress_unimplemented,
	.supported = stress_tsc_supported,
	.classifier = CLASS_CPU,
	.verify = VERIFY_OPTIONAL,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without RISC-V rdtime, x86 rdtsc, s390 stck instructions or powerpc __ppc_get_timebase()"
};
#endif
