// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-killpid.h"

#define MAX_SIGSUSPEND_PIDS	(4)

static const stress_help_t help[] = {
	{ NULL,	"sigsuspend N",	    "start N workers exercising sigsuspend" },
	{ NULL,	"sigsuspend-ops N", "stop after N bogo sigsuspend wakes" },
	{ NULL,	NULL,		    NULL }
};

static void *counter_lock;

static void stress_sigsuspend_chld_handler(int sig)
{
	(void)sig;

	stress_continue_set_flag(false);
}

/*
 *  stress_sigsuspend
 *	stress sigsuspend
 */
static int stress_sigsuspend(const stress_args_t *args)
{
	pid_t pid[MAX_SIGSUSPEND_PIDS];
	size_t n, i;
	sigset_t mask, oldmask;
	int rc = EXIT_SUCCESS, parent_cpu;

	if (stress_sighandler(args->name, SIGUSR1, stress_sighandler_nop, NULL) < 0)
		return EXIT_FAILURE;
	if (stress_sighandler(args->name, SIGCHLD, stress_sigsuspend_chld_handler, NULL) < 0)
		return EXIT_FAILURE;

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf_skip("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)sigemptyset(&mask);
	(void)sigprocmask(SIG_BLOCK, &mask, &oldmask);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (n = 0; n < MAX_SIGSUSPEND_PIDS; n++) {
again:
		parent_cpu = stress_get_cpu();
		pid[n] = fork();
		if (pid[n] < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			if (!stress_continue(args))
				goto reap;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto reap;
		} else if (pid[n] == 0) {
			(void)stress_change_cpu(args, parent_cpu);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			do {
				int ret;

				ret = sigsuspend(&mask);
				if (UNLIKELY((ret < 0) && (errno != EINTR)))
					_exit(EXIT_FAILURE);
			} while (stress_bogo_inc_lock(args, counter_lock, true));
			_exit(0);
		}
	}

	/* Parent */
	do {
		for (i = 0; (i < n) && stress_bogo_inc_lock(args, counter_lock, false); i++) {
			(void)shim_kill(pid[i], SIGUSR1);
		}
	} while (stress_continue(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	for (i = 0; i < n; i++) {
		int status;

		if (waitpid(pid[i], &status, WNOHANG) == pid[i]) {
			if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) != EXIT_SUCCESS) {
					pr_fail("%s: sigsuspend() failed unexpectedly\n",
						args->name);
					rc = EXIT_FAILURE;
				}
				continue;
			}
		}

		if (shim_kill(pid[i], 0) == 0) {
			/* terminate child */
			stress_force_killed_bogo(args);
			(void)stress_kill_pid_wait(pid[i], NULL);
		} else {
			if (shim_waitpid(pid[i], &status, 0) == 0) {
				pr_inf("%jd died prematurely\n", (intmax_t)pid[i]);
			}
		}
	}

	(void)stress_lock_destroy(counter_lock);

	return rc;
}

stressor_info_t stress_sigsuspend_info = {
	.stressor = stress_sigsuspend,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
