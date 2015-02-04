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

#if defined(__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>

#include "stress-ng.h"

/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */
int stress_affinity(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	unsigned long int cpu = 0;
	cpu_set_t mask;

	(void)instance;
	(void)name;

	do {
		cpu = (opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
			(mwc() >> 4) : cpu + 1;
		cpu %= stress_get_processors_online();
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
			pr_fail(stderr, "failed to move to CPU %lu\n", cpu);
		} else {
			/* Now get and check */
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			sched_getaffinity(0, sizeof(mask), &mask);
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    (!CPU_ISSET(cpu, &mask)))
				pr_fail(stderr, "failed to move to CPU %lu\n", cpu);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
