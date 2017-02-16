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

#if defined(__linux__) && NEED_GLIBC(2,3,0)

/*
 *  stress on sched_affinity()
 *	stress system by changing CPU affinity periodically
 */
int stress_affinity(const args_t *args)
{
	uint32_t cpu = args->instance;
	const uint32_t cpus = stress_get_processors_configured();
	cpu_set_t mask;

	do {
		cpu = (g_opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
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
			pr_fail("%s: failed to move to CPU %" PRIu32
				", errno=%d (%s)\n",
				args->name, cpu, errno, strerror(errno));
			(void)shim_sched_yield();
		} else {
			/* Now get and check */
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
				if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
				    (!CPU_ISSET(cpu, &mask)))
					pr_fail("%s: failed to move "
						"to CPU %" PRIu32 "\n",
						args->name, cpu);
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_affinity(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
