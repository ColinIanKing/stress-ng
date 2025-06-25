/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"sigchld N",	 "start N workers that handle SIGCHLD" },
	{ NULL,	"sigchld-ops N", "stop after N bogo SIGCHLD signals" },
	{ NULL,	NULL,		 NULL }
};

static uint64_t counter;
static uint64_t cld_exited;
static uint64_t cld_killed;
static uint64_t cld_stopped;
static uint64_t cld_continued;

static void MLOCKED_TEXT stress_sigchld_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if (sig != SIGCHLD)
		return;

	switch (info->si_code) {
	case CLD_EXITED:
		cld_exited++;
		break;
	case CLD_KILLED:
		cld_killed++;
		break;
	case CLD_STOPPED:
		cld_stopped++;
		break;
	case CLD_CONTINUED:
		cld_continued++;
		break;
	default:
		break;
	}
	counter++;
}

/*
 *  stress_sigchld
 *	stress by generating SIGCHLD signals on exiting
 *	child processes.
 */
static int stress_sigchld(stress_args_t *args)
{
	struct sigaction sa;

	counter = 0;
	cld_exited = 0;
	cld_killed = 0;
	cld_stopped = 0;
	cld_continued = 0;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigchld_handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		pr_err("%s: cannot install SIGCHLD handler, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (UNLIKELY(!stress_continue(args)))
				goto finish;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			/* Child immediately exits */
			_exit(EXIT_FAILURE);
		} else {
			/* Parent wait and reap for child */
			if (shim_kill(pid, SIGSTOP) == 0)
				(void)shim_kill(pid, SIGCONT);
			(void)stress_kill_pid_wait(pid, NULL);
		}
		stress_bogo_set(args, counter);
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	stress_metrics_set(args, 0, "child exited", (double)cld_exited, STRESS_METRIC_TOTAL);
	stress_metrics_set(args, 1, "child killed", (double)cld_killed, STRESS_METRIC_TOTAL);
	stress_metrics_set(args, 2, "child stopped", (double)cld_stopped, STRESS_METRIC_TOTAL);
	stress_metrics_set(args, 3, "child continued", (double)cld_continued, STRESS_METRIC_TOTAL);

#if !defined(__OpenBSD__)
	/*
	 *  No si_code codes recognised and we handled SIGCHLD signals, then
	 *  something is not conformant
	 */
	if ((cld_exited + cld_killed + cld_stopped + cld_continued == 0) &&
	    (counter > 0)) {
		pr_fail("%s: no SIGCHLD siginfo si_code detected in signal handler\n",
			args->name);
		return EXIT_FAILURE;
	}
#endif

	return EXIT_SUCCESS;
}

const stressor_info_t stress_sigchld_info = {
	.stressor = stress_sigchld,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
