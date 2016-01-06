/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#if defined(STRESS_TSC)

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
		pr_inf(stderr, "tsc stressor will be skipped, "
			"not a recognised Intel CPU.\n");
		return -1;
	}
	/* ..and supports tsc? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(edx & 0x10)) {
		pr_inf(stderr, "tsc stressor will be skipped, CPU "
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
int stress_tsc(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	(void)instance;
	(void)name;

	if (tsc_supported) {
		do {
			TSCx32();
			TSCx32();
			TSCx32();
			TSCx32();
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
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
	pr_inf(stderr, "tsc stressor will be skipped, CPU does not "
		"support the tsc instruction.\n");
	return -1;
}

/*
 *  stress_tsc()
 *      no-op for non-intel
 */
int stress_tsc(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	(void)counter;
	(void)instance;
	(void)max_ops;
	(void)name;

	return EXIT_SUCCESS;
}
#endif
