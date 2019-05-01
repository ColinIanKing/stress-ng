/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"sigrt N",	"start N workers sending real time signals" },
	{ NULL,	"sigrt-ops N",	"stop after N real time signal bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SIGQUEUE) &&		\
    defined(HAVE_SIGWAITINFO) &&	\
    defined(SIGRTMIN) &&		\
    defined(SIGRTMAX)

#define MAX_RTPIDS (SIGRTMAX - SIGRTMIN + 1)

static void MLOCKED_TEXT stress_sigrthandler(int signum)
{
	(void)signum;
}

/*
 *  stress_sigrt
 *	stress by heavy real time sigqueue message sending
 */
static int stress_sigrt(const args_t *args)
{
	pid_t pids[MAX_RTPIDS];
	union sigval s;
	int i, status;

	(void)memset(pids, 0, sizeof pids);

	for (i = 0; i < MAX_RTPIDS; i++) {
		if (stress_sighandler(args->name, i + SIGRTMIN, stress_sigrthandler, NULL) < 0) {
			return EXIT_FAILURE;
		}
	}

	for (i = 0; i < MAX_RTPIDS; i++) {
again:
		pids[i] = fork();
		if (pids[i] < 0) {
			if (g_keep_stressing_flag && (errno == EAGAIN))
				goto again;
			pr_fail_dbg("fork");
			goto reap;
		} else if (pids[i] == 0) {
			sigset_t mask;
			siginfo_t info;

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			(void)sigemptyset(&mask);
			for (i = 0; i < MAX_RTPIDS; i++)
				(void)sigaddset(&mask, i + SIGRTMIN);

			while (g_keep_stressing_flag) {
				(void)memset(&info, 0, sizeof info);

				if (sigwaitinfo(&mask, &info) < 0) {
					if (errno == EINTR)
						continue;
					break;
				}
				if (info.si_value.sival_int == 0)
					break;

				if (info.si_value.sival_int != -1) {
					(void)memset(&s, 0, sizeof(s));
					s.sival_int = -1;
					(void)sigqueue(info.si_value.sival_int, SIGRTMIN, s);
				}
			}
			/*
			pr_dbg("%s: child got termination notice (%d)\n", args->name, info.si_value.sival_int);
			pr_dbg("%s: exited on pid [%d] (instance %" PRIu32 ")\n",
				args->name, getpid(), args->instance);
			*/
			_exit(0);
		}
	}

	/* Parent */
	do {
		for (i = 0; i < MAX_RTPIDS; i++) {
			const int pid = pids[(i + 1) % MAX_RTPIDS];

			(void)memset(&s, 0, sizeof(s));

			/* Inform child which pid to queue a signal to */
			s.sival_int = pid;
			(void)sigqueue(pids[i], i + SIGRTMIN, s);
			inc_counter(args);
		}
	} while (keep_stressing());

	for (i = 0; i < MAX_RTPIDS; i++) {
		if (pids[i] > 0) {
			(void)memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			(void)sigqueue(pids[i], i + SIGRTMIN, s);
		}
	}
	(void)shim_usleep(250);
reap:
	for (i = 0; i < MAX_RTPIDS; i++) {
		if (pids[i] > 0) {
			/* And ensure child is really dead */
			(void)kill(pids[i], SIGKILL);
			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigrt_info = {
	.stressor = stress_sigrt,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sigrt_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
