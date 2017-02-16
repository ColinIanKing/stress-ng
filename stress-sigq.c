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

#if _POSIX_C_SOURCE >= 199309L && !defined(__gnu_hurd__)

static void MLOCKED stress_sigqhandler(int dummy)
{
	(void)dummy;
}

/*
 *  stress_sigq
 *	stress by heavy sigqueue message sending
 */
int stress_sigq(const args_t *args)
{
	pid_t pid;

	if (stress_sighandler(args->name, SIGUSR1, stress_sigqhandler, NULL) < 0)
		return EXIT_FAILURE;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		sigemptyset(&mask);
		sigaddset(&mask, SIGUSR1);

		while (g_keep_stressing_flag) {
			siginfo_t info;
			sigwaitinfo(&mask, &info);
			if (info.si_value.sival_int)
				break;
		}
		pr_dbg("%s: child got termination notice\n", args->name);
		pr_dbg("%s: exited on pid [%d] (instance %" PRIu32 ")\n",
			args->name, getpid(), args->instance);
		_exit(0);
	} else {
		/* Parent */
		union sigval s;
		int status;

		do {
			memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			sigqueue(pid, SIGUSR1, s);
			inc_counter(args);
		} while (keep_stressing());

		pr_dbg("%s: parent sent termination notice\n", args->name);
		memset(&s, 0, sizeof(s));
		s.sival_int = 1;
		sigqueue(pid, SIGUSR1, s);
		(void)shim_usleep(250);
		/* And ensure child is really dead */
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
#else
int stress_sigq(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
