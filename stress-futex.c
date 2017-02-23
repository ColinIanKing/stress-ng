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

#if defined(__linux__) && defined(__NR_futex)

#include <linux/futex.h>

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
 *  stress_futex()
 *	stress system by futex calls. The intention is not to
 * 	efficiently use futex, but to stress the futex system call
 *	by rapidly calling it on wait and wakes
 */
int stress_futex(const args_t *args)
{
	uint64_t *timeout = &g_shared->futex.timeout[args->instance];
	uint32_t *futex = &g_shared->futex.futex[args->instance];
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (pid > 0) {
		int status;

		(void)setpgid(pid, g_pgrp);

		do {
			int ret;

			/*
			 * Break early in case wake gets stuck
			 * (which it shouldn't)
			 */
			if (!g_keep_stressing_flag)
				break;
			ret = futex_wake(futex, 1);
			if (g_opt_flags & OPT_FLAGS_VERIFY) {
				if (ret < 0)
					pr_fail_err("futex wake");
			}
		} while (keep_stressing());

		/* Kill waiter process */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);

		pr_dbg("%s: futex timeouts: %" PRIu64 "\n",
			args->name, *timeout);
	} else {
		uint64_t threshold = THRESHOLD;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		do {
			/* Small timeout to force rapid timer wakeups */
			const struct timespec t = { .tv_sec = 0, .tv_nsec = 5000 };
			int ret;

			/* Break early before potential long wait */
			if (!g_keep_stressing_flag)
				break;

			ret = futex_wait(futex, 0, &t);

			/* timeout, re-do, stress on stupid fast polling */
			if ((ret < 0) && (errno == ETIMEDOUT)) {
				(*timeout)++;
				if (*timeout > threshold) {
					/*
					 * Backoff for a short while
					 * and start again
					 */
					(void)shim_usleep(250000);
					threshold += THRESHOLD;
				}
			} else {
				if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
					pr_fail_err("futex wait");
				}
				inc_counter(args);
			}
		} while (keep_stressing());
	}

	return EXIT_SUCCESS;
}
#else
int stress_futex(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
