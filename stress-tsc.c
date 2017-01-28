/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(STRESS_X86) && !defined(__OpenBSD__) && NEED_GNUC(4,6,0)

static bool tsc_supported = false;

#include <cpuid.h>

/*
 *  stress_tsc_supported()
 *	check if tsc is supported
 */
int stress_tsc_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	__cpuid(0, eax, ebx, ecx, edx);
	if (!((memcmp(&ebx, "Genu", 4) == 0) &&
	      (memcmp(&edx, "ineI", 4) == 0) &&
	      (memcmp(&ecx, "ntel", 4) == 0))) {
		pr_inf("tsc stressor will be skipped, "
			"not a recognised Intel CPU.\n");
		return -1;
	}
	/* ..and supports tsc? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(edx & 0x10)) {
		pr_inf("tsc stressor will be skipped, CPU "
			"does not support the rdtsc instruction.\n");
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
int stress_tsc(const args_t *args)
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

#else

/*
 *  stress_tsc_supported()
 *	check if tsc is supported
 */
int stress_tsc_supported(void)
{
	pr_inf("tsc stressor will be skipped, CPU does not "
		"support the tsc instruction.\n");
	return -1;
}

/*
 *  stress_tsc()
 *      no-op for non-intel
 */
int stress_tsc(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
