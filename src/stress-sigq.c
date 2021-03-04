/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"sigq N",	"start N workers sending sigqueue signals" },
	{ NULL,	"sigq-ops N",	"stop after N sigqueue bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SIGQUEUE) && \
    defined(HAVE_SIGWAITINFO)

static void MLOCKED_TEXT stress_sigqhandler(int signum)
{
	(void)signum;
}

/*
 *  stress_sigq
 *	stress by heavy sigqueue message sending
 */
static int stress_sigq(const stress_args_t *args)
{
	pid_t pid;

	if (stress_sighandler(args->name, SIGUSR1, stress_sigqhandler, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;
		int i = 0;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)sigemptyset(&mask);
		(void)sigaddset(&mask, SIGUSR1);
		if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) {
			pr_fail("%s: sigprocmask failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			_exit(EXIT_FAILURE);
		}

		while (keep_stressing_flag()) {
			siginfo_t info;
			int ret;

			(void)memset(&info, 0, sizeof(info));

			if (i++ & 1) {
				ret = sigwaitinfo(&mask, &info);
				if (ret < 0)
					break;
			} else {
				struct timespec timeout;

				timeout.tv_sec = 1;
				timeout.tv_nsec = 0;

				ret = sigtimedwait(&mask, &info, &timeout);
				if (ret < 0) {
					if (errno == EAGAIN)
						continue;
					break;
				}
			}
			if (info.si_value.sival_int)
				break;
			if (info.si_signo != SIGUSR1)
				break;
		}
		pr_dbg("%s: child got termination notice\n", args->name);
		pr_dbg("%s: exited on pid [%d] (instance %" PRIu32 ")\n",
			args->name, (int)getpid(), args->instance);
		_exit(0);
	} else {
		/* Parent */
		union sigval s;
		int status;

		do {
			(void)memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			(void)sigqueue(pid, SIGUSR1, s);
			inc_counter(args);
		} while (keep_stressing(args));

		pr_dbg("%s: parent sent termination notice\n", args->name);
		(void)memset(&s, 0, sizeof(s));
		s.sival_int = 1;
		(void)sigqueue(pid, SIGUSR1, s);
		(void)shim_usleep(250);
		/* And ensure child is really dead */
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigq_info = {
	.stressor = stress_sigq,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sigq_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
