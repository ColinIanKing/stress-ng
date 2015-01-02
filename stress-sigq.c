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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>

#include "stress-ng.h"

#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)
static void stress_sigqhandler(int dummy)
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
	struct sigaction new_action;

	new_action.sa_handler = stress_sigqhandler;
	sigemptyset(&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction(SIGUSR1, &new_action, NULL) < 0) {
		pr_failed_err(name, "sigaction");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;

		sigemptyset(&mask);
		sigaddset(&mask, SIGUSR1);

		for (;;) {
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
