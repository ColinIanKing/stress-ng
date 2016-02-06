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

#if defined(STRESS_AFFINITY)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sched.h>


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
	uint32_t cpu = instance;
	const uint32_t cpus = stress_get_processors_configured();
	cpu_set_t mask;

	do {
		cpu = (opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
			(mwc32() >> 4) : cpu + 1;
		cpu %= cpus;
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		if (sched_setaffinity(0, sizeof(mask), &mask) < 0) {
			if (errno == EINVAL) {
				/*
				 * We get this if CPU is offline'd,
				 * and since that can be dynamically
				 * set, we should just retry
				 */
				continue;
			}
			pr_fail(stderr, "%s: failed to move to CPU %" PRIu32
				", errno=%d (%s)\n",
				name, cpu, errno, strerror(errno));
#if defined(_POSIX_PRIORITY_SCHEDULING)
			sched_yield();
#endif
		} else {
			/* Now get and check */
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			sched_getaffinity(0, sizeof(mask), &mask);
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    (!CPU_ISSET(cpu, &mask)))
				pr_fail(stderr, "%s: failed to move to CPU %" PRIu32 "\n",
					name, cpu);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
