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

#if defined(STRESS_FUTEX)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <linux/futex.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/syscall.h>

#define THRESHOLD	(100000)

/*
 *  futex wake()
 *	wake n waiters on futex
 */
static inline int futex_wake(const void *futex, const int n)
{
	return syscall(SYS_futex, futex, FUTEX_WAKE, n, NULL, NULL, 0);
}

/*
 *  futex_wait()
 *	wait on futex with a timeout
 */
static inline int futex_wait(
	const void *futex,
	const int val,
	const struct timespec *timeout)
{
	return syscall(SYS_futex, futex, FUTEX_WAIT, val, timeout, NULL, 0);
}

/*
 *  stress_fuxex()
 *	stress system by futex calls. The intention is not to
 * 	efficiently use futex, but to stress the futex system call
 *	by rapidly calling it on wait and wakes
 */
int stress_futex(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint64_t *timeout = &shared->futex.timeout[instance];
	uint32_t *futex = &shared->futex.futex[instance];
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	}
	if (pid > 0) {
		int status;

		setpgid(pid, pgrp);

		do {
			int ret;

			/*
			 * Break early in case wake gets stuck
			 * (which it shouldn't)
			 */
			if (!opt_do_run)
				break;
			ret = futex_wake(futex, 1);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (ret < 0)
					pr_fail(stderr, "%s: futex wake failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* Kill waiter process */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);

		pr_dbg(stderr, "%s: futex timeouts: %" PRIu64 "\n", name, *timeout);
	} else {
		uint64_t threshold = THRESHOLD;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		do {
			/* Small timeout to force rapid timer wakeups */
			const struct timespec t = { .tv_sec = 0, .tv_nsec = 5000 };
			int ret;

			/* Break early before potential long wait */
			if (!opt_do_run)
				break;

			ret = futex_wait(futex, 0, &t);

			/* timeout, re-do, stress on stupid fast polling */
			if ((ret < 0) && (errno == ETIMEDOUT)) {
				(*timeout)++;
				if (*timeout > threshold) {
					/* Backoff for a short while and start again */
					usleep(250000);
					threshold += THRESHOLD;
				}
			} else {
				if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail(stderr, "%s: futex wait "
						"failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
				(*counter)++;
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}

	return EXIT_SUCCESS;
}

#endif
