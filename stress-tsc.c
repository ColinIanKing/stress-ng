/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"tsc N",	"start N workers reading the TSC (x86 only)" },
	{ NULL,	"tsc-ops N",	"stop after N TSC bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_CPUID_H) &&	\
    defined(STRESS_X86) && 	\
    defined(HAVE_CPUID) &&	\
    NEED_GNUC(4,6,0)

static bool tsc_supported = false;

/*
 *  stress_tsc_supported()
 *	check if tsc is supported
 */
static int stress_tsc_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	if (!cpu_is_x86()) {
		pr_inf("tsc stressor will be skipped, "
			"not a recognised Intel CPU\n");
		return -1;
	}
	/* ..and supports tsc? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(edx & 0x10)) {
		pr_inf("tsc stressor will be skipped, CPU "
			"does not support the rdtsc instruction\n");
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
static int stress_tsc(const args_t *args)
{
	if (tsc_supported) {
		do {
			TSCx32();
			TSCx32();
			TSCx32();
			TSCx32();
			inc_counter(args);
		} while (keep_stressing());
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_tsc,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.help = help
};
#else

static int stress_tsc_supported(void)
{
	pr_inf("tsc stressor will be skipped, CPU "
		"does not support the rdtsc instruction.\n");
	return -1;
}

stressor_info_t stress_tsc_info = {
	.stressor = stress_not_implemented,
	.supported = stress_tsc_supported,
	.class = CLASS_CPU,
	.help = help
};
#endif
