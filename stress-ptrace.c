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
#include "core-killpid.h"

#include <time.h>

#if defined(HAVE_PTRACE)
#include <sys/ptrace.h>
#endif

#if defined(HAVE_PTRACE_REQUEST)
#define shim_ptrace_request	enum __ptrace_request
#else
#define shim_ptrace_request	int
#endif

static const stress_help_t help[] = {
	{ NULL,	"ptrace N",	"start N workers that trace a child using ptrace" },
	{ NULL,	"ptrace-ops N",	"stop ptrace workers after N system calls are traced" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_PTRACE)

/*
 *  main syscall ptrace loop
 */
static inline bool OPTIMIZE3 stress_syscall_wait(
	stress_args_t *args,
	const pid_t pid)
{
	while (stress_continue_flag()) {
		int status;

		if (UNLIKELY(ptrace(PTRACE_SYSCALL, pid, 0, 0) < 0)) {
			if ((errno != ESRCH) && (errno != EPERM) && (errno != EACCES)) {
				pr_fail("%s: ptrace failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return true;
			}
		}
		if (UNLIKELY(shim_waitpid(pid, &status, 0) < 0)) {
			if ((errno != EINTR) && (errno != ECHILD))
				pr_fail("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
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
static int OPTIMIZE3 stress_ptrace(stress_args_t *args)
{
	pid_t pid;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/*
		 * Child to be traced, we abort if we detect
		 * we are already being traced by someone else
		 * as this makes life way too complex
		 */
		if (ptrace(PTRACE_TRACEME) != 0) {
			pr_inf_skip("%s: child cannot be traced, "
				"skipping stressor, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			_exit(EXIT_SUCCESS);
		}
		/* Wait for parent to start tracing me */
		(void)shim_kill(getpid(), SIGSTOP);

		/*
		 *  A simple mix of system calls
		 */
		while (stress_continue_flag()) {
			VOID_RET(pid_t, getppid());
#if defined(HAVE_GETPGRP)
			VOID_RET(pid_t, getpgrp());
#endif
			VOID_RET(gid_t, getgid());
			VOID_RET(gid_t, getegid());
			VOID_RET(uid_t, getuid());
			VOID_RET(uid_t, geteuid());
			VOID_RET(time_t, time(NULL));
		}
		_exit(0);
	} else {
		/* Parent to do the tracing */
		int status;
		int i = 0;

		if (shim_waitpid(pid, &status, 0) < 0) {
			if ((errno != EINTR) && (errno != ECHILD)) {
				pr_fail("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
					args->name, (intmax_t)pid, errno, strerror(errno));
				return EXIT_FAILURE;
			}
			return EXIT_SUCCESS;
		}
		if (ptrace(PTRACE_SETOPTIONS, pid,
			0, PTRACE_O_TRACESYSGOOD) < 0) {
			pr_inf_skip("%s: child cannot be traced, "
				"skipping stressor, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			if ((errno == ESRCH) || (errno == EPERM) || (errno == EACCES)) {
				/* Ensure child is really dead and reap */
				(void)stress_kill_pid(pid);
				if (shim_waitpid(pid, &status, 0) < 0) {
					if ((errno != EINTR) && (errno != ECHILD)) {
						pr_fail("%s: waitpid() on PID %" PRIdMAX " failed, errno=%d (%s)\n",
							args->name, (intmax_t)pid, errno, strerror(errno));
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

			/* periodicially perform invalid ptrace calls */
			if (UNLIKELY((i & 0x1ff) == 0)) {
				const pid_t bad_pid = stress_get_unused_pid_racy(false);

				/* exercise invalid options */
				VOID_RET(long int, ptrace((shim_ptrace_request)~0L, pid, 0, PTRACE_O_TRACESYSGOOD));

				/* exercise invalid pid */
				VOID_RET(long int, ptrace(PTRACE_SETOPTIONS, bad_pid, 0, PTRACE_O_TRACESYSGOOD));
			}
			i++;

			stress_bogo_inc(args);
		} while (stress_continue(args));

		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_ptrace_info = {
	.stressor = stress_ptrace,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_ptrace_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without ptrace() system call support"
};
#endif
