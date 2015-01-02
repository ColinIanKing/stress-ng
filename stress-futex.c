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

#if defined (__linux__)

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

#include "stress-ng.h"

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
static inline int futex_wait(const void *futex, const int val, const struct timespec *timeout)
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
	uint64_t *timeout = &shared->futex_timeout[instance];
	uint32_t *futex = &shared->futex[instance];
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	}
	if (pid > 0) {
		int status;

		do {
			int ret;

			/* Break early in case wake gets stuck (which it shouldn't) */
			if (!opt_do_run)
				break;
			ret = futex_wake(futex, 1);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (ret < 0)
					pr_fail(stderr, "futex wake failed: errno=%d (%s)\n",
						errno, strerror(errno));
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* Kill waiter process */
		kill(pid, SIGKILL);
		waitpid(pid, &status, 0);

		pr_dbg(stderr, "futex timeouts: %" PRIu64 "\n", *timeout);
	} else {
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
				continue;
			}
	
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "futex wait failed: errno=%d (%s)\n",
					errno, strerror(errno));
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}

	return EXIT_SUCCESS;
}

#endif
