// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

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
static int stress_sigchld(const stress_args_t *args)
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

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto finish;
			pr_err("%s: fork failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		} else if (pid == 0) {
			/* Child immediately exits */
			_exit(EXIT_FAILURE);
		} else {
			/* Parent wait and reap for child */
			int wstatus, ret;

			ret = shim_kill(pid, SIGSTOP);
			if (ret == 0) {
				VOID_RET(int, shim_kill(pid, SIGCONT));
			}
			VOID_RET(int, shim_kill(pid, SIGKILL));
			VOID_RET(int, waitpid(pid, &wstatus, 0));
		}
		stress_bogo_set(args, counter);
	} while (stress_continue(args));

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	pr_dbg("%s: exit: %" PRIu64 ", kill: %" PRIu64
		", stop: %" PRIu64 ", continue: %" PRIu64 "\n",
		args->name,
		cld_exited, cld_killed,
		cld_stopped, cld_continued);

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

stressor_info_t stress_sigchld_info = {
	.stressor = stress_sigchld,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
