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
	uint64_t *counters;
	volatile uint64_t *v_counters;
	const size_t counters_size =
		(sizeof(*counters) * MAX_SIGSUSPEND_PIDS) << CACHE_STRIDE_SHIFT;

	if (stress_sighandler(args->name, SIGUSR1, stress_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	v_counters = counters = (uint64_t *)mmap(NULL, counters_size,
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_err("%s: mmap failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	(void)memset(counters, 0, counters_size);

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
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			while (keep_stressing_flag()) {
				(void)sigsuspend(&mask);
				v_counters[n << CACHE_STRIDE_SHIFT]++;
			}
			_exit(0);
		}
		(void)setpgid(pid[n], g_pgrp);
	}

	/* Parent */
	do {
		set_counter(args, 0);
		for (i = 0; (i < n) && keep_stressing(args); i++) {
			add_counter(args, v_counters[i << CACHE_STRIDE_SHIFT]);
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
	(void)munmap((void *)counters, counters_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigsuspend_info = {
	.stressor = stress_sigsuspend,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
