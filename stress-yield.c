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

#if defined(STRESS_YIELD)

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sched.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#if defined(__sun__)
#include <alloca.h>
#endif
#if defined(__linux__) && NEED_GLIBC(2,3,0)
#include <sched.h>
#endif

/*
 *  stress on sched_yield()
 *	stress system by sched_yield
 */
int stress_yield(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint64_t *counters;
	uint64_t max_ops_per_yielder;
	size_t counters_sz, yielders_sz;
	int32_t cpus = stress_get_processors_configured();
	size_t instances = stressor_instances(STRESS_YIELD);
	size_t yielders = 2;
#if defined(__linux__) && NEED_GLIBC(2,3,0)
	cpu_set_t mask;
#endif
	pid_t *pids;
	size_t i;

#if defined(__linux__) && NEED_GLIBC(2,3,0)
	/*
	 *  If the process is limited to a subset of cores
	 *  then make sure we do not create too many yielders
	 */
	if (sched_getaffinity(0, sizeof(mask), &mask) < 0) {
		pr_inf(stderr, "%s: can't get sched affinity, defaulting to %"
			PRId32 " yielder%s (instance %" PRIu32 ")\n",
			name, cpus, (cpus == 1) ? "" : "s", instance);
	} else {
		if (CPU_COUNT(&mask) < cpus)
			cpus = CPU_COUNT(&mask);
		pr_inf(stderr, "%s: limiting to %" PRId32 " yielder%s (instance %"
			PRIu32 ")\n", name, cpus, (cpus == 1) ? "" : "s", instance);
	}
#endif

	/*
	 *  Ensure we always have at least 2 yielders per
	 *  CPU available to force context switching on yields
	 */
	if (cpus > 0) {
		if (!instances) {
			/* Should not happen, but avoids division by zero */
			instances = 1;
		}
		cpus *= 2;
		yielders = cpus / instances;
		if (yielders < 1)
			yielders = 1;
		if (!instance) {
			int32_t residual = cpus - (yielders * instances);
			if (residual > 0)
				yielders += residual;
		}
	}

	max_ops_per_yielder = max_ops / yielders;
	yielders_sz = yielders * sizeof(pid_t);
	pids = alloca(yielders_sz);
	memset(pids, 0, yielders_sz);

	counters_sz = yielders * sizeof(uint64_t);
	counters = (uint64_t *)mmap(NULL, counters_sz, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		int rc = exit_status(errno);

		pr_err(stderr, "%s: mmap failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return rc;
	}
	memset(counters, 0, counters_sz);

	for (i = 0; opt_do_run && (i < yielders); i++) {
		pids[i] = fork();
		if (pids[i] < 0) {
			pr_dbg(stderr, "%s: fork failed (instance %" PRIu32
				", yielder %zd): errno=%d (%s)\n",
				name, instance, i, errno, strerror(errno));
		} else if (pids[i] == 0) {
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			do {
				int ret;

				ret = sched_yield();
				if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
					pr_fail(stderr, "%s: sched_yield failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
				counters[i]++;
			} while (opt_do_run && (!max_ops_per_yielder || *counter < max_ops_per_yielder));
			_exit(EXIT_SUCCESS);
		}
	}

	do {
		*counter = 0;
		pause();
		for (i = 0; i < yielders; i++)
			*counter += counters[i];
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/* Parent, wait for children */
	for (i = 0; i < yielders; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)waitpid(pids[i], &status, 0);
			*counter += counters[i];
		}
	}
	(void)munmap((void *)counters, counters_sz);

	return EXIT_SUCCESS;
}
#endif
