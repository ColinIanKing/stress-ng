/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"sigstop N",     "start N workers sending SIGSTOP signals" },
	{ NULL,	"sigstop-ops N", "stop after N kill SIGSTOP and SIGKILL bogo operations" },
	{ NULL,	NULL,            NULL }
};

static pid_t stress_sigstop_pid;
static stress_args_t *stress_sigstop_args;

static void MLOCKED_TEXT OPTIMIZE3 stress_sigstop_chld_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if ((sig == SIGCHLD) &&
	    info &&
	    (info->si_pid == stress_sigstop_pid)) {
		/* continue the stopped child */
		(void)kill(stress_sigstop_pid, SIGCONT);
		if (stress_sigstop_args && stress_continue(stress_sigstop_args))
			stress_bogo_inc(stress_sigstop_args);
	}
}

static void MLOCKED_TEXT OPTIMIZE3 stress_sigstop_cont_handler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	(void)ucontext;

	if ((sig == SIGCONT) &&
	    info &&
	    (info->si_pid == stress_sigstop_pid) &&
	    stress_sigstop_args &&
	    stress_continue(stress_sigstop_args))
		stress_bogo_inc(stress_sigstop_args);
}

/*
 *  stress_sigstop
 *	stress by heavy SIGSTOP/SIGCONT signals
 */
static int stress_sigstop(stress_args_t *args)
{
	int rc = EXIT_SUCCESS, parent_cpu;
	struct sigaction sa;

	stress_sigstop_args = args;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigstop_chld_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGCHLD, &sa, NULL) < 0) {
		pr_err("%s: cannot install SIGCHLD handler, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigstop_cont_handler;
	sa.sa_flags = SA_SIGINFO;
	if (sigaction(SIGCONT, &sa, NULL) < 0) {
		pr_err("%s: cannot install SIGCONT, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_cpu_get();
	stress_sigstop_pid = fork();
	if (stress_sigstop_pid < 0) {
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		if (stress_redo_fork(args, errno))
			goto again;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (stress_sigstop_pid == 0) {
		const pid_t my_pid = getpid();

		stress_proc_state_set(args->name, STRESS_STATE_RUN);
		stress_make_it_fail_set();
		(void)stress_affinity_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)stress_sched_settings_apply(true);

		while (stress_continue(args)) {
			/* stop myself! */
			(void)kill(my_pid, SIGSTOP);
		}
		_exit(rc);
	} else {
		/* Parent */
		int status;

		do {
			pause();
		} while (stress_continue(args));

		(void)stress_kill_pid_wait(stress_sigstop_pid, &status);
		if (WIFEXITED(status))
			if (WEXITSTATUS(status) == EXIT_FAILURE)
				rc = EXIT_FAILURE;
	}

finish:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("context-switches"),
	STRESS_EX_FEATURE("stack"),

	STRESS_EX_SYSCALL("kill"),
	STRESS_EX_SYSCALL("pause"),
#if defined(__linux__)
	STRESS_EX_SYSCALL("sigreturn"),
#endif
	STRESS_EX_END,
};

const stressor_info_t stress_sigstop_info = {
	.stressor = stress_sigstop,
	.classifier = CLASS_SIGNAL | CLASS_OS | CLASS_IPC,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.exercises = exercises,
};
