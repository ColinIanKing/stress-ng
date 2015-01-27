/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include "stress-ng.h"

#if defined(STRESS_X86) && !defined(__OpenBSD__)

#include <cpuid.h>

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
#define RDRAND64()	\
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
	uint32_t eax, ebx, ecx, edx;

	(void)instance;

	/* Intel CPU? */
	__cpuid(0, eax, ebx, ecx, edx);
	if (!((memcmp(&ebx, "Genu", 4) == 0) &&
	      (memcmp(&edx, "ineI", 4) == 0) &&
	      (memcmp(&ecx, "ntel", 4) == 0))) {
		pr_err(stderr, "%s: rdrand test aborted, not a recognised Intel CPU.\n", name);
		return EXIT_FAILURE;
	}
	/* ..and supports rdrand? */
	__cpuid(1, eax, ebx, ecx, edx);
	if (!(ecx & 0x40000000)) {
		pr_err(stderr, "%s: rdrand test aborted, CPU does not support rdrand instruction.\n", name);
		return EXIT_FAILURE;
	}

	do {
		RDRAND64();
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_dbg(stderr, "%s: %" PRIu64 " random bits read\n",
		name,  (*counter) * 64 * 32);

	return EXIT_SUCCESS;
}

#else
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

	pr_dbg(stderr, "%s: rdrand instruction not supported on this architecture\n", name);

	return EXIT_SUCCESS;
}
#endif
