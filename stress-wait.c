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
	{ NULL,	"wait N",	"start N workers waiting on child being stop/resumed" },
	{ NULL,	"wait-ops N",	"stop after N bogo wait operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  Disabled for GNU/Hurd because this stressor breaks with
 *  the error:
 *    intr-msg.c:387: _hurd_intr_rpc_mach_msg: Assertion
 *    `m->header.msgh_id == msgid + 100'
 */
#if !defined(__gnu_hurd__)

#define ABORT_TIMEOUT	(0.0025)

/*
 *  spawn()
 *	spawn a process
 */
static pid_t spawn(
	stress_args_t *args,
	void (*func)(stress_args_t *args, const pid_t pid),
	const pid_t pid_arg)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return -1;
	}
	if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_parent_died_alarm();

		func(args, pid_arg);
		_exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  runner()
 *	this process pauses, but is continually being
 *	stopped and continued by the killer process
 */
static void NORETURN runner(
	stress_args_t *args,
	const pid_t pid)
{
	(void)pid;

	pr_dbg("%s: runner started [%" PRIdMAX "]\n", args->name, (intmax_t)getpid());

	do {
		(void)shim_pause();
	} while (stress_continue(args));

	(void)shim_kill(getppid(), SIGALRM);
	_exit(EXIT_SUCCESS);
}

/*
 *  killer()
 *	this continually stops and continues the runner process
 */
static void NORETURN killer(
	stress_args_t *args,
	const pid_t pid)
{
	double start = stress_time_now();
	uint64_t last_counter = stress_bogo_get(args);
	pid_t ppid = getppid();

	pr_dbg("%s: killer started [%" PRIdMAX "]\n", args->name, (intmax_t)getpid());

	do {
		(void)shim_kill(pid, SIGSTOP);
		(void)shim_sched_yield();
		(void)shim_kill(pid, SIGCONT);

		/*
		 *  The waits may be blocked and
		 *  so the counter is not being updated.
		 *  If it is blocked for too long bail out
		 *  so we don't get stuck in the parent
		 *  waiter indefinitely.
		 */
		if (last_counter == stress_bogo_get(args)) {
			const double now = stress_time_now();

			if (now - start > ABORT_TIMEOUT) {
				/* unblock waiting parent */
				(void)shim_kill(ppid, SIGUSR1);
				start = now;
			}
		} else {
			start = stress_time_now();
			last_counter = stress_bogo_get(args);
		}
	} while (stress_continue(args));

	/* forcefully kill runner, wait is in parent */
	(void)stress_kill_pid(pid);

	/* tell parent to wake up! */
	(void)shim_kill(getppid(), SIGALRM);
	_exit(EXIT_SUCCESS);
}

/*
 *  stress_wait_continued()
 *	check WIFCONTINUED
 */
static inline void stress_wait_continued(stress_args_t *args, int status)
{
#if defined(WIFCONTINUED)
	if (WIFCONTINUED(status))
		stress_bogo_inc(args);
#else
	(void)status;

	stress_bogo_inc(args);
#endif
}

/*
 *  syscall_shim_waitpid
 *	waitpid that prefers waitpid syscall if it is available
 *	over the libc waitpid that may use wait4 instead
 */
static pid_t syscall_shim_waitpid(pid_t pid, int *wstatus, int options)
{
#if defined(__NR_waitpid) &&	\
    defined(HAVE_SYSCALL)
	return (pid_t)syscall(__NR_waitpid, pid, wstatus, options);
#else
	return waitpid(pid, wstatus, options);
#endif
}

/*
 *  stress_wait
 *	stress wait*() family of calls
 */
static int stress_wait(stress_args_t *args)
{
	int ret = EXIT_SUCCESS;
	pid_t pid_r, pid_k, wret;
	int options = 0;
#if defined(HAVE_WAIT4)
	const pid_t pgrp = getpgrp();
#endif

#if defined(WUNTRACED)
	options |= WUNTRACED;
#endif
#if defined(WCONTINUED)
	options |= WCONTINUED;
#endif

	pr_dbg("%s: waiter started [%" PRIdMAX "]\n",
		args->name, (intmax_t)args->pid);

	if (stress_sighandler(args->name, SIGUSR1, stress_sighandler_nop, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pid_r = spawn(args, runner, 0);
	if (pid_r < 0) {
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	pid_k = spawn(args, killer, pid_r);
	if (pid_k < 0) {
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		ret = EXIT_FAILURE;
		goto tidy;
	}

	do {
		int status;
#if defined(HAVE_WAIT4) || defined(HAVE_WAIT3)
		struct rusage usage;
#endif
		/*
		 *  Exercise waitpid
		 */
		wret = syscall_shim_waitpid(pid_r, &status, options);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: waitpid on PID %" PRIdMAX " failed, errno=%d (%s)\n",
				args->name, (intmax_t)pid_r, errno, strerror(errno));
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise wait
		 */
		wret = shim_wait(&status);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;
#if defined(HAVE_WAIT3)
		/*
		 *  Exercise wait3 if available
		 */
		wret = shim_wait3(&status, options, &usage);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait3 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;
#endif

#if defined(HAVE_WAIT4)
		/*
		 *  Exercise wait4 if available
		 */
		wret = shim_wait4(pid_r, &status, options, &usage);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait4 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise PID -1 -> any child process
		 */
		wret = shim_wait4(-1, &status, options, &usage);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait4 on PID -1 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise PID 0 -> process group of caller
		 */
		wret = shim_wait4(0, &status, options, &usage);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait4 on PID 0 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise -ve PID -> PGID number
		 */
		wret = shim_wait4(-pgrp, &status, options, &usage);
		if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
			pr_fail("%s: wait4 on pgrp %" PRIdMAX " failed, errno=%d (%s)\n",
				args->name, (intmax_t)pgrp, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		stress_wait_continued(args, status);
		if (UNLIKELY(!stress_continue(args)))
			break;

		/*
		 *  Exercise wait4 with invalid PID, errno -> ESRCH
		 */
		VOID_RET(int, shim_wait4(INT_MIN, &status, options, &usage));

		/*
		 *  Exercise wait4 with invalid options, errno -> EINVAL
		 */
		VOID_RET(int, shim_wait4(0, &status, ~0, &usage));
#endif

#if defined(HAVE_WAITID)
		/*
		 *  Exercise waitid if available
		 */
		if (options) {
			siginfo_t info;

			(void)shim_memset(&info, 0, sizeof(info));
			wret = waitid(P_PID, (id_t)pid_r, &info, options);
			if (UNLIKELY((wret < 0) && (errno != EINTR) && (errno != ECHILD))) {
				pr_fail("%s: waitid failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				ret = EXIT_FAILURE;
				break;
			}
			/*
			 *  Need to look into this, but on a heavily loaded
			 *  system we can get info.si_pid set to zero(!)
			 */
			if (UNLIKELY((info.si_pid != pid_r) && (info.si_pid != 0))) {
				pr_fail("%s: waitid returned PID %ld but expected PID %ld\n",
					args->name, (long int)info.si_pid, (long int)pid_r);
				ret = EXIT_FAILURE;
			}
			if (UNLIKELY((info.si_signo != SIGCHLD) && (info.si_signo != 0))) {
				pr_fail("%s: waitid returned si_signo %d (%s) but expected SIGCHLD\n",
					args->name, info.si_signo, stress_strsignal(info.si_signo));
				ret = EXIT_FAILURE;
			}
			if (UNLIKELY((info.si_status != EXIT_SUCCESS) &&
				     (info.si_status != SIGSTOP) &&
				     (info.si_status != SIGCONT) &&
				     (info.si_status != SIGKILL))) {
				pr_fail("%s: waitid returned unexpected si_status %d\n",
					args->name, info.si_status);
				ret = EXIT_FAILURE;
			}
#if defined(CLD_EXITED) &&	\
    defined(CLD_KILLED) &&	\
    defined(CLD_STOPPED) &&	\
    defined(CLD_CONTINUED)
			if (UNLIKELY((info.si_code != CLD_EXITED) &&
				     (info.si_code != CLD_KILLED) &&
				     (info.si_code != CLD_STOPPED) &&
				     (info.si_code != CLD_CONTINUED) &&
				     (info.si_code != 0))) {
				pr_fail("%s: waitid returned unexpected si_code %d\n",
					args->name, info.si_code);
				ret = EXIT_FAILURE;
			}
#endif
			stress_wait_continued(args, status);
		}
#endif
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_kill_pid_wait(pid_k, NULL);
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_kill_pid_wait(pid_r, NULL);

	return ret;
}

const stressor_info_t stress_wait_info = {
	.stressor = stress_wait,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_wait_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "disabled for GNU/HURD because it causes the kernel to assert"
};
#endif
