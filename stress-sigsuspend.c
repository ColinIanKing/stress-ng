/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#define MAX_SIGSUSPEND_PIDS	(4)
#define CACHE_STRIDE_SHIFT	(6)

static const stress_help_t help[] = {
	{ NULL,	"sigsuspend N",	    "start N workers exercising sigsuspend" },
	{ NULL,	"sigsuspend-ops N", "stop after N bogo sigsuspend wakes" },
	{ NULL,	NULL,		    NULL }
};

static void *counter_lock;

/*
 *  stress_usr1_handler()
 *      SIGUSR1 handler
 */
static void MLOCKED_TEXT stress_usr1_handler(int signum)
{
	(void)signum;
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
	int status;

	if (stress_sighandler(args->name, SIGUSR1, stress_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	counter_lock = stress_lock_create();
	if (!counter_lock) {
		pr_inf("%s: failed to create counter lock. skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)sigemptyset(&mask);
	(void)sigprocmask(SIG_BLOCK, &mask, &oldmask);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (n = 0; n < MAX_SIGSUSPEND_PIDS; n++) {
again:
		pid[n] = fork();
		if (pid[n] < 0) {
			if (stress_redo_fork(errno))
				goto again;
			if (!keep_stressing(args))
				goto reap;
			pr_err("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto reap;
		} else if (pid[n] == 0) {
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			do {
				(void)sigsuspend(&mask);
			} while (inc_counter_lock(args, counter_lock, true));
			_exit(0);
		}
	}

	/* Parent */
	do {
		for (i = 0; (i < n) && inc_counter_lock(args, counter_lock, false); i++) {
			(void)kill(pid[i], SIGUSR1);
		}
	} while (keep_stressing(args));

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	for (i = 0; i < n; i++) {
		/* terminate child */
		(void)kill(pid[i], SIGKILL);
		(void)shim_waitpid(pid[i], &status, 0);
	}
	(void)stress_lock_destroy(counter_lock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigsuspend_info = {
	.stressor = stress_sigsuspend,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
