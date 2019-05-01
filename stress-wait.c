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
	{ NULL,	"wait N",	"start N workers waiting on child being stop/resumed" },
	{ NULL,	"wait-ops N",	"stop after N bogo wait operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  Disabled for GNU/Hurd because it this stressor breaks with
 *  the error:
 *    intr-msg.c:387: _hurd_intr_rpc_mach_msg: Assertion
 *    `m->header.msgh_id == msgid + 100'
 */
#if !defined(__gnu_hurd__)

#define ABORT_TIMEOUT	(1.0)

static void MLOCKED_TEXT stress_usr1_handler(int signum)
{
	(void)signum;
}

/*
 *  spawn()
 *	spawn a process
 */
static pid_t spawn(
	const args_t *args,
	void (*func)(const args_t *args, const pid_t pid),
	pid_t pid_arg)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		return -1;
	}
	if (pid == 0) {
		stress_parent_died_alarm();

		func(args, pid_arg);
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  runner()
 *	this process pauses, but is continually being
 *	stopped and continued by the killer process
 */
static void runner(
	const args_t *args,
	const pid_t pid)
{
	(void)pid;

	pr_dbg("%s: wait: runner started [%d]\n", args->name, (int)getpid());

	do {
		(void)pause();
	} while (keep_stressing());

	(void)kill(getppid(), SIGALRM);
	_exit(EXIT_SUCCESS);
}

/*
 *  killer()
 *	this continually stops and continues the runner process
 */
static void killer(
	const args_t *args,
	const pid_t pid)
{
	double start = time_now();
	uint64_t last_counter = get_counter(args);
	pid_t ppid = getppid();

	pr_dbg("%s: wait: killer started [%d]\n", args->name, (int)getpid());

	do {
		(void)kill(pid, SIGSTOP);
		(void)shim_sched_yield();
		(void)kill(pid, SIGCONT);

		/*
		 *  The waits may be blocked and
		 *  so the counter is not being updated.
		 *  If it is blocked for too long bail out
		 *  so we don't get stuck in the parent
		 *  waiter indefinitely.
		 */
		if (last_counter == get_counter(args)) {
			const double now = time_now();
			if (now - start > ABORT_TIMEOUT) {
				/* unblock waiting parent */
				(void)kill(ppid, SIGUSR1);
				start = now;
			}
		} else {
			start = time_now();
			last_counter = get_counter(args);
		}
	} while (keep_stressing());

	/* forcefully kill runner, wait is in parent */
	(void)kill(pid, SIGKILL);

	/* tell parent to wake up! */
	(void)kill(getppid(), SIGALRM);
	_exit(EXIT_SUCCESS);
}

/*
 *  stress_wait
 *	stress wait*() family of calls
 */
static int stress_wait(const args_t *args)
{
	int status, ret = EXIT_SUCCESS;
	pid_t pid_r, pid_k, wret;
	int options = 0;

#if defined(WUNTRACED)
	options |= WUNTRACED;
#endif
#if defined(WCONTINUED)
	options |= WCONTINUED;
#endif

	pr_dbg("%s: waiter started [%d]\n",
		args->name, (int)args->pid);

	if (stress_sighandler(args->name, SIGUSR1, stress_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	pid_r = spawn(args, runner, 0);
	if (pid_r < 0) {
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	}

	pid_k = spawn(args, killer, pid_r);
	if (pid_k < 0) {
		pr_fail_dbg("fork");
		ret = EXIT_FAILURE;
		goto tidy;
	}

	do {
		wret = waitpid(pid_r, &status, options);
		if ((wret < 0) && (errno != EINTR) && (errno != ECHILD)) {
			pr_fail_dbg("waitpid()");
			break;
		}
		if (!g_keep_stressing_flag)
			break;
#if defined(WIFCONINUED)
		if (WIFCONTINUED(status))
			inc_counter(args);
#else
		inc_counter(args);
#endif

#if defined(HAVE_WAITID)
		if (options) {
			siginfo_t info;

			wret = waitid(P_PID, pid_r, &info, options);
			if ((wret < 0) && (errno != EINTR) && (errno != ECHILD)) {
				pr_fail_dbg("waitpid()");
				break;
			}
			if (!g_keep_stressing_flag)
				break;
#if defined(WIFCONTINUED)
			if (WIFCONTINUED(status))
				inc_counter(args);
#else
			inc_counter(args);
#endif
		}
#endif
	} while (g_keep_stressing_flag && (!args->max_ops || get_counter(args) < args->max_ops));

	(void)kill(pid_k, SIGKILL);
	(void)shim_waitpid(pid_k, &status, 0);
tidy:
	(void)kill(pid_r, SIGKILL);
	(void)shim_waitpid(pid_r, &status, 0);

	return ret;
}

stressor_info_t stress_wait_info = {
	.stressor = stress_wait,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_wait_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
