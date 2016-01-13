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

#if defined(STRESS_UNSHARE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define MAX_PIDS	(32)

#define UNSHARE(flags)	\
	sys_unshare(name, flags, #flags);

/*
 *  unshare with some error checking
 */
void sys_unshare(const char *name, int flags, const char *flags_name)
{
	int rc;
#if NEED_GLIBC(2,14,0)
	rc = unshare(flags);
#else
	rc = syscall(__NR_unshare, flags);
#endif
	if ((rc < 0) && (errno != EPERM) && (errno != EINVAL)) {
		pr_fail(stderr, "%s: unshare(%s) failed, "
			"errno=%d (%s)\n", name, flags_name,
			errno, strerror(errno));
	}
}

/*
 *  stress_unshare()
 *	stress resource unsharing
 */
int stress_unshare(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_PIDS];

	(void)instance;

	do {
		size_t i, n;

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

#if defined(CLONE_FS)
				UNSHARE(CLONE_FS);
#endif
#if defined(CLONE_FILES)
				UNSHARE(CLONE_FILES);
#endif
#if defined(CLONE_NEWIPC)
				UNSHARE(CLONE_NEWIPC);
#endif
#if defined(CLONE_NEWNET)
				UNSHARE(CLONE_NEWNET);
#endif
#if defined(CLONE_NEWNS)
				UNSHARE(CLONE_NEWNS);
#endif
#if defined(CLONE_NEWPID)
				UNSHARE(CLONE_NEWPID);
#endif
#if defined(CLONE_NEWUSER)
				UNSHARE(CLONE_NEWUSER);
#endif
#if defined(CLONE_NEWUTS)
				UNSHARE(CLONE_NEWUTS);
#endif
#if defined(CLONE_SYSVSEM)
				UNSHARE(CLONE_SYSVSEM);
#endif
#if defined(CLONE_THREAD)
				UNSHARE(CLONE_THREAD);
#endif
#if defined(CLONE_SIGHAND)
				UNSHARE(CLONE_SIGHAND);
#endif
#if defined(CLONE_VM)
				UNSHARE(CLONE_VM);
#endif
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
