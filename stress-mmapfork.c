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

#if defined(STRESS_MMAPFORK)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>

#define MAX_PIDS	(32)

/*
 *  stress_mmapfork()
 *	stress mappings + fork VM subystem
 */
int stress_mmapfork(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_PIDS];
	struct sysinfo info;
	void *ptr;
	int32_t instances;

	(void)instance;

	if ((instances = stressor_instances(STRESS_MMAPFORK)) < 1)
		instances = stress_get_processors_configured();

	do {
		size_t i, n, len;

		memset(pids, 0, sizeof(pids));

		for (n = 0; n < MAX_PIDS; n++) {
retry:			if (!opt_do_run)
				goto reap;

			pids[n] = fork();
			if (pids[n] < 0) {
				/* Out of resources for fork, re-do, ugh */
				if (errno == EAGAIN) {
					usleep(10000);
					goto retry;
				}
				break;
			}
			if (pids[n] == 0) {
				/* Child */
				setpgid(0, pgrp);
				stress_parent_died_alarm();

				if (sysinfo(&info) < 0) {
					pr_fail_err(name, "sysinfo");
					_exit(0);
				}
				len = ((size_t)info.freeram / (instances * MAX_PIDS)) / 2;
				ptr = mmap(NULL, len, PROT_READ | PROT_WRITE,
					MAP_POPULATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (ptr != MAP_FAILED) {
					madvise(ptr, len, MADV_WILLNEED);
					memset(ptr, 0, len);
					madvise(ptr, len, MADV_DONTNEED);
					munmap(ptr, len);
				}
				_exit(0);
			}
			setpgid(pids[n], pgrp);
		}
reap:
		for (i = 0; i < n; i++) {
			int status;

			if (waitpid(pids[i], &status, 0) < 0) {
				if (errno != EINTR)
					pr_err(stderr, "%s: waitpid errno=%d (%s)\n",
						name, errno, strerror(errno));
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
