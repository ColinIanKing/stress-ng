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

#if defined(STRESS_SIGQUEUE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

static void MLOCKED stress_sigqhandler(int dummy)
{
	(void)dummy;
}

/*
 *  stress_sigq
 *	stress by heavy sigqueue message sending
 */
int stress_sigq(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;

	if (stress_sighandler(name, SIGUSR1, stress_sigqhandler, NULL) < 0)
		return EXIT_FAILURE;

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		sigemptyset(&mask);
		sigaddset(&mask, SIGUSR1);

		while (opt_do_run) {
			siginfo_t info;
			sigwaitinfo(&mask, &info);
			if (info.si_value.sival_int)
				break;
		}
		pr_dbg(stderr, "%s: child got termination notice\n", name);
		pr_dbg(stderr, "%s: exited on pid [%d] (instance %" PRIu32 ")\n",
			name, getpid(), instance);
		_exit(0);
	} else {
		/* Parent */
		union sigval s;
		int status;

		do {
			memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			sigqueue(pid, SIGUSR1, s);
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		pr_dbg(stderr, "%s: parent sent termination notice\n", name);
		memset(&s, 0, sizeof(s));
		s.sival_int = 1;
		sigqueue(pid, SIGUSR1, s);
		usleep(250);
		/* And ensure child is really dead */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
#endif
