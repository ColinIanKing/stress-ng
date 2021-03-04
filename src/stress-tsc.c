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
	{ NULL,	"tsc N",	"start N workers reading the time stamp counter" },
	{ NULL,	"tsc-ops N",	"stop after N TSC bogo operations" },
	{ NULL,	NULL,		NULL }
};


#if defined(STRESS_ARCH_RISC_V) &&	\
    defined(SIGILL)

#define HAVE_STRESS_TSC_CAPABILITY

static sigjmp_buf jmpbuf;
static bool tsc_supported = false;

static inline unsigned long rdtsc(void)
{
	register unsigned long cycles;

        __asm__ __volatile__("rdcycle %0"
                              : "=r" (cycles)
			      :
                              : "memory");
	return cycles;
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
		pr_inf("%s stressor will be skipped, "
			"rdcycle not allowed\n", name);
		return -1;
	}

	cycles = rdtsc();
	(void)cycles;
	tsc_supported = true;

	return 0;
}
#endif

#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_ARCH_X86) && 	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)

#define HAVE_STRESS_TSC_CAPABILITY

static bool tsc_supported = false;

/*
 *  stress_tsc_supported()
 *	check if tsc is supported, x86 variant
 */
static int stress_tsc_supported(const char *name)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	if (!stress_cpu_is_x86()) {
		pr_inf("%s stressor will be skipped, "
			"not a recognised Intel CPU\n", name);
		return -1;
	}
	/* ..and supports tsc? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(edx & 0x10)) {
		pr_inf("%s stressor will be skipped, CPU "
			"does not support the tsc instruction\n", name);
		return -1;
	}
	tsc_supported = true;
	return 0;
}

/*
 *  read tsc
 */
static inline void rdtsc(void)
{
#if STRESS_TSC_SERIALIZED
	asm volatile("cpuid\nrdtsc\n" : : : "%edx", "%eax");
#else
	asm volatile("rdtsc\n" : : : "%edx", "%eax");
#endif
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

static inline void rdtsc(void)
{
	(void)__ppc_get_timebase();
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

static inline void rdtsc(void)
{
	uint64_t tick;

	asm("\tstck\t%0\n" : "=Q" (tick) : : "cc");
}
#endif

#if defined(HAVE_STRESS_TSC_CAPABILITY)
/*
 *  Unrolled 32 times
 */
#define TSCx32()	\
{			\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
	rdtsc();	\
}

/*
 *  stress_tsc()
 *      stress Intel tsc instruction
 */
static int stress_tsc(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (tsc_supported) {
		do {
			TSCx32();
			TSCx32();
			TSCx32();
			TSCx32();
			inc_counter(args);
		} while (keep_stressing(args));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_tsc,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_tsc_supported(const char *name)
{
	pr_inf("%s stressor will be skipped, CPU "
		"does not support the rdtsc instruction.\n", name);
	return -1;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_not_implemented,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.help = help
};
#endif
