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
	{ NULL,	"ptrace N",	"start N workers that trace a child using ptrace" },
	{ NULL,	"ptrace-ops N",	"stop ptrace workers after N system calls are traced" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_PTRACE)

/*
 *  main syscall ptrace loop
 */
static inline bool stress_syscall_wait(
	const stress_args_t *args,
	const pid_t pid)
{
	while (keep_stressing_flag()) {
		int status;

		if (ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0) {
			if ((errno != ESRCH) && (errno != EPERM) && (errno != EACCES)) {
				pr_fail("%s: ptrace failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return true;
			}
		}
		if (shim_waitpid(pid, &status, 0) < 0) {
			if ((errno != EINTR) && (errno != ECHILD))
				pr_fail("%s: waitpid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			return true;
		}

		if (WIFSTOPPED(status) &&
		    (WSTOPSIG(status) & 0x80))
			return false;
		if (WIFEXITED(status))
			return true;
	}
	return true;
}

/*
 *  stress_ptrace()
 *	stress ptracing
 */
static int stress_ptrace(const stress_args_t *args)
{
	pid_t pid;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pid = fork();
	if (pid < 0) {
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/*
		 * Child to be traced, we abort if we detect
		 * we are already being traced by someone else
		 * as this makes life way too complex
		 */
		if (ptrace(PTRACE_TRACEME) != 0) {
			pr_inf("%s: child cannot be traced, "
				"skipping stressor: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			_exit(EXIT_SUCCESS);
		}
		/* Wait for parent to start tracing me */
		(void)kill(getpid(), SIGSTOP);

		/*
		 *  A simple mix of system calls
		 */
		while (keep_stressing_flag()) {
			pid_t pidtmp;
			gid_t gidtmp;
			uid_t uidtmp;
			time_t ttmp;

			pidtmp = getppid();
			(void)pidtmp;

#if defined(HAVE_GETPGRP)
			pidtmp = getpgrp();
			(void)pidtmp;
#endif

			gidtmp = getgid();
			(void)gidtmp;

			gidtmp = getegid();
			(void)gidtmp;

			uidtmp = getuid();
			(void)uidtmp;

			uidtmp = geteuid();
			(void)uidtmp;

			ttmp = time(NULL);
			(void)ttmp;
		}
		_exit(0);
	} else {
		/* Parent to do the tracing */
		int status;

		(void)setpgid(pid, g_pgrp);

		if (shim_waitpid(pid, &status, 0) < 0) {
			if ((errno != EINTR) && (errno != ECHILD)) {
				pr_fail("%s: waitpid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}
		if (ptrace(PTRACE_SETOPTIONS, pid,
			0, PTRACE_O_TRACESYSGOOD) < 0) {
			pr_inf("%s: child cannot be traced, "
				"skipping stressor: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			if ((errno == ESRCH) || (errno == EPERM) || (errno == EACCES)) {
				/* Ensure child is really dead and reap */
				(void)kill(pid, SIGKILL);
				if (shim_waitpid(pid, &status, 0) < 0) {
					if ((errno != EINTR) && (errno != ECHILD)) {
						pr_fail("%s: waitpid failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						return EXIT_FAILURE;
					}
					return EXIT_SUCCESS;
				}
				return WEXITSTATUS(status);
			}
			pr_fail("%s: ptrace failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		do {
			/*
			 *  We do two of the following per syscall,
			 *  one at the start, and one at the end to catch
			 *  the return.  In this stressor we don't really
			 *  care which is which, we just care about counting
			 *  them
			 */
			if (stress_syscall_wait(args, pid))
				break;
			inc_counter(args);
		} while (keep_stressing(args));

		/* Terminate child */
		(void)kill(pid, SIGKILL);
		if (shim_waitpid(pid, &status, 0) < 0)
			pr_fail("%s: waitpid failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_ptrace_info = {
	.stressor = stress_ptrace,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_ptrace_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
