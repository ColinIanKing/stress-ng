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

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <sched.h>
#include <errno.h>
#include <time.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"


/*
 *  stress on sched_nice()
 *	stress system by sched_nice
 */
int stress_nice(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int max_prio, min_prio;

	(void)instance;
	(void)name;

#ifdef RLIMIT_NICE
	{
		struct rlimit rlim;

		if (getrlimit(RLIMIT_NICE, &rlim) < 0) {
			/* Make an assumption, bah */
			max_prio = 20;
			min_prio = -20;
		} else {
			max_prio = 20 - (int)rlim.rlim_cur;
			min_prio = -max_prio;
		}
	}
#else
	/* Make an assumption, bah */
	max_prio = 20;
	min_prio = -20;
#endif

	do {
		pid_t pid;

		pid = fork();
		if (pid == 0) {
			int i;

			/* Child */
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			pid = getpid();
			for (i = min_prio; i <= max_prio; i++) {
				errno = 0;
				setpriority(PRIO_PROCESS, pid, i);
				if (!errno) {
					double start = time_now();
					double delay = 0.05 + (double)mwc16() / 327680.0;

					while (time_now() - start < delay)
						;
					(*counter)++;
				}
			}
			_exit(0);
		}
		if (pid > 0) {
			int status;

			setpgid(pid, pgrp);

			/* Parent, wait for child */
			if (waitpid(pid, &status, 0) < 0) {
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
			}
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
