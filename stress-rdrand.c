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

#if defined(STRESS_RDRAND)

static bool rdrand_supported = false;

#include <cpuid.h>

/*
 *  stress_rdrand_supported()
 *	check if rdrand is supported
 */
int stress_rdrand_supported(void)
{
	uint32_t eax, ebx, ecx, edx;

	/* Intel CPU? */
	__cpuid(0, eax, ebx, ecx, edx);
	if (!((memcmp(&ebx, "Genu", 4) == 0) &&
	      (memcmp(&edx, "ineI", 4) == 0) &&
	      (memcmp(&ecx, "ntel", 4) == 0))) {
		pr_inf(stderr, "rdrand stressor will be skipped, "
			"not a recognised Intel CPU.\n");
		return -1;
	}
	/* ..and supports rdrand? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(ecx & 0x40000000)) {
		pr_inf(stderr, "rdrand stressor will be skipped, CPU "
			"does not support the rdrand instruction.\n");
		return -1;
	}
	rdrand_supported = true;
	return 0;
}

/*
 *  rdrand64()
 *	read 64 bit random value
 */
static inline uint64_t rdrand64(void)
{
        uint64_t        ret;

        asm volatile("1:;\n\
        rdrand %0;\n\
        jnc 1b;\n":"=r"(ret));

        return ret;
}

/*
 *  Unrolled 32 times
 */
#define RDRAND64x32()	\
{			\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
	rdrand64();	\
}

/*
 *  stress_rdrand()
 *      stress Intel rdrand instruction
 */
int stress_rdrand(
        uint64_t *const counter,
        const uint32_t instance,
        const uint64_t max_ops,
        const char *name)
{
	if (rdrand_supported) {
		double time_start, duration, billion_bits;

		time_start = time_now();
		do {
			RDRAND64x32();
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		duration = time_now() - time_start;
		billion_bits = ((double)*counter * 64.0 * 32.0) / 1000000000.0;

		pr_dbg(stderr, "%s: %.3f billion random bits read "
			"(instance %" PRIu32")\n",
			name, billion_bits, instance);
		if (duration > 0.0) {
			pr_dbg(stderr, "%s: %.3f billion random bits per "
				"second (instance %" PRIu32")\n",
				name, (double)billion_bits / duration, instance);
		}
	}
	return EXIT_SUCCESS;
}

#else

/*
 *  stress_rdrand_supported()
 *	check if rdrand is supported
 */
int stress_rdrand_supported(void)
{
	pr_inf(stderr, "rdrand stressor will be skipped, CPU does not "
		"support the rdrand instruction.\n");
	return -1;
}

/*
 *  stress_rdrand()
 *      no-op for non-intel
 */
int stress_rdrand(
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
