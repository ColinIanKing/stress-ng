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

#if defined(STRESS_WAIT)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#define ABORT_TIMEOUT	(8.0)

/*
 *  spawn()
 *	spawn a process
 */
static pid_t spawn(
	const char *name,
	void (*func)(const char *name, const pid_t pid,
		uint64_t *counter, const uint64_t max_ops),
	pid_t pid_arg,
	uint64_t *counter,
	uint64_t max_ops)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		return -1;
	}
	if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		func(name, pid_arg, counter, max_ops);
		exit(EXIT_SUCCESS);
	}
	setpgid(pid, pgrp);
	return pid;
}

/*
 *  runner()
 *	this process pauses, but is continually being
 *	stopped and continued by the killer process
 */
static void runner(
	const char *name,
	const pid_t pid,
	uint64_t *counter,
	const uint64_t max_ops)
{
	(void)pid;

	pr_dbg(stderr, "%s: wait: runner started [%d]\n", name, getpid());

	do {
		(void)pause();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	kill(getppid(), SIGALRM);
	exit(EXIT_SUCCESS);
}

/*
 *  killer()
 *	this continually stops and continues the runner process
 */
static void killer(
	const char *name,
	const pid_t pid,
	uint64_t *counter,
	const uint64_t max_ops)
{
	double start = time_now();
	uint64_t last_counter = *counter;

	pr_dbg(stderr, "%s: wait: killer started [%d]\n", name, getpid());

	do {
		(void)kill(pid, SIGSTOP);
		(void)kill(pid, SIGCONT);

		/*
		 *  The waits may be blocked and
		 *  so the counter is not being updated.
		 *  If it is blocked for too long bail out
		 *  so we don't get stuck in the parent
		 *  waiter indefintely.
		 */
		if (last_counter == *counter) {
			if (time_now() - start > ABORT_TIMEOUT) {
				pr_dbg(stderr, "%s: waits were blocked, "
					"aborting\n", name);
				break;
			}
		} else {
			start = time_now();
			last_counter = *counter;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/* forcefully kill runner, wait is in parent */
	(void)kill(pid, SIGKILL);
	/* tell parent to wake up! */
	(void)kill(getppid(), SIGALRM);
	exit(EXIT_SUCCESS);
}

/*
 *  stress_wait
 *	stress wait*() family of calls
 */
int stress_wait(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int status, ret = EXIT_SUCCESS;
	pid_t pid_r, pid_k;

	(void)instance;

	pr_dbg(stderr, "%s: waiter started [%d]\n",
		name, getpid());

	pid_r = spawn(name, runner, 0, counter, max_ops);
	if (pid_r < 0) {
		pr_fail_dbg(name, "fork");
		exit(EXIT_FAILURE);
	}

	pid_k = spawn(name, killer, pid_r, counter, max_ops);
	if (pid_k < 0) {
		pr_fail_dbg(name, "fork");
		ret = EXIT_FAILURE;
		goto tidy;
	}

	do {
		(void)waitpid(pid_r, &status, WCONTINUED);
		if (!opt_do_run)
			break;
		if (WIFCONTINUED(status))
			(*counter)++;

#if _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
    _XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED || \
    _POSIX_C_SOURCE >= 200809L
		{
			siginfo_t info;

			(void)waitid(P_PID, pid_r, &info, WCONTINUED);
			if (!opt_do_run)
				break;
			if (WIFCONTINUED(status))
				(*counter)++;
		}
#endif
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)kill(pid_k, SIGKILL);
	(void)waitpid(pid_k, &status, 0);
tidy:
	(void)kill(pid_r, SIGKILL);
	(void)waitpid(pid_r, &status, 0);

	return ret;
}

#endif
