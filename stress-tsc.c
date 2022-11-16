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
	{ NULL,	NULL,		NULL }
};

#if defined(STRESS_ARCH_RISCV) &&	\
    defined(SIGILL)

#define HAVE_STRESS_TSC_CAPABILITY

static sigjmp_buf jmpbuf;
static bool tsc_supported = false;

static inline uint64_t rdtsc(void)
{
	register unsigned long ticks;

        __asm__ __volatile__("rdtime %0"
                              : "=r" (ticks)
			      :
                              : "memory");
	return (uint64_t)ticks;
}

static void stress_sigill_handler(int signum)
{
	(void)signum;

	siglongjmp(jmpbuf, 1);
}

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, riscv variant
 */
static int stress_tsc_supported(const char *name)
{
	unsigned long cycles;

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
    !defined(__PCC__) &&		\
    !defined(__TINYC__)

#define HAVE_STRESS_TSC_CAPABILITY

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
static uint64_t rdtsc(void)
{
#if defined(STRESS_TSC_SERIALIZED)
	__asm__ __volatile__("cpuid\n");
#endif
#if defined(STRESS_ARCH_X86_64)
	uint32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
#endif
#if defined(STRESS_ARCH_X86_32)
	uint64_t tsc;

	__asm__ __volatile__("rdtsc" : "=A" (tsc));
	return tsc;
#endif
	return 0;
}

#elif defined(STRESS_ARCH_PPC64) &&		\
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
	uint64_t tick;

	__asm__ __volatile__("\tstck\t%0\n" : "=Q" (tick) : : "cc");

	return tick;
}
#endif

static inline void stress_tsc_check(
	const stress_args_t *args,
	const uint64_t tsc,
	const uint64_t old_tsc)
{
	if (LIKELY(tsc > old_tsc))
		return;

	/* top bits different, tsc -> wrapped around? */
	if (((old_tsc ^ tsc) >> 63) == 1)
		return;

	pr_fail("%s: TSC not monitonically increasing, TSC %" PRIx64 " vs previous TSC %" PRIx64 "\n",
		args->name, tsc, old_tsc);
}

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
#define TSCx32_verify(args, tsc, old_tsc)	\
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
	stress_tsc_check(args, tsc, old_tsc);	\
	old_tsc = tsc;	\
} while (0)

/*
 *  stress_tsc()
 *      stress Intel tsc instruction
 */
static int stress_tsc(const stress_args_t *args)
{

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (tsc_supported) {
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
		double duration = 0.0, count;


		if (verify) {
			uint64_t tsc, old_tsc;

			old_tsc = rdtsc();
			do {
				const double t = stress_time_now();

				TSCx32_verify(args, tsc, old_tsc);
				TSCx32_verify(args, tsc, old_tsc);
				TSCx32_verify(args, tsc, old_tsc);
				TSCx32_verify(args, tsc, old_tsc);

				duration += stress_time_now() - t;
				inc_counter(args);
			} while (keep_stressing(args));
		} else {
			do {
				const double t = stress_time_now();

				TSCx32();
				TSCx32();
				TSCx32();
				TSCx32();

				duration += stress_time_now() - t;
				inc_counter(args);
			} while (keep_stressing(args));
		}
		count = 32.0 * 4.0 * (double)get_counter(args);
		duration = (count > 0.0) ? duration / count : 0.0;
		stress_misc_stats_set(args->misc_stats, 0, "nanosecs per time counter read", duration * 1000000000.0);
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_tsc,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else

static int stress_tsc_supported(const char *name)
{
	pr_inf_skip("%s stressor will be skipped, CPU "
		"does not support the rdtsc instruction.\n", name);
	return -1;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_unimplemented,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without RISC-V rdtime, x86 rdtsc, s390 stck instructions or powerpc __ppc_get_timebase()"
};
#endif
