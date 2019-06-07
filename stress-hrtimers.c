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
	{ NULL,	"hrtimers N",	  "start N workers that exercise high resolution timers" },
	{ NULL,	"hrtimers-ops N", "stop after N bogo high-res timer bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
static volatile uint64_t *timer_counter;
static uint64_t max_ops;
static timer_t timerid;
static double start;

#define PROCS_MAX	(32)

/*
 *  stress_hrtimers_set()
 *	set timer, ensure it is never zero
 */
static void stress_hrtimers_set(struct itimerspec *timer)
{
	timer->it_value.tv_sec = 0;
	timer->it_value.tv_nsec = (mwc64() % 50000) + 1;
	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_hrtimers_keep_stressing()
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_hrtimers_keep_stressing(void)
{
        return (LIKELY(g_keep_stressing_flag) &&
                LIKELY(!max_ops || ((*timer_counter) < max_ops)));
}

/*
 *  stress_hrtimers_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT stress_hrtimers_handler(int sig)
{
	struct itimerspec timer;
	sigset_t mask;

	(void)sig;

	if (!stress_hrtimers_keep_stressing())
		goto cancel;
	(*timer_counter)++;

	if (sigpending(&mask) == 0)
		if (sigismember(&mask, SIGINT))
			goto cancel;
	/* High freq timer, check periodically for timeout */
	if (((*timer_counter) & 65535) == 0)
		if ((time_now() - start) > (double)g_opt_timeout)
			goto cancel;
	stress_hrtimers_set(&timer);
	return;

cancel:
	g_keep_stressing_flag = false;
	/* Cancel timer if we detect no more runs */
	(void)memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
}

/*
 *  stress_hrtimer_process
 *	stress timer child process
 */
static int stress_hrtimer_process(const args_t *args, uint64_t *counter)
{
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	int ret;

	timer_counter = counter;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	ret = stress_set_sched(getpid(), SCHED_RR, UNDEFINED, true);
	(void)ret;

	start = time_now();
	if (stress_sighandler(args->name, SIGRTMIN, stress_hrtimers_handler, NULL) < 0)
		return EXIT_FAILURE;

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_fail_err("timer_create");
		return EXIT_FAILURE;
	}

	stress_hrtimers_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail_err("timer_settime");
		return EXIT_FAILURE;
	}

	do {
		/* The sleep will be interrupted on each hrtimer tick */
		(void)sleep(1);
	} while (stress_hrtimers_keep_stressing());

	if (timer_delete(timerid) < 0) {
		pr_fail_err("timer_delete");
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_hrtimers(const args_t *args)
{
	pid_t pids[PROCS_MAX];
	const size_t page_size = args->page_size;
	const size_t counters_sz = sizeof(uint64_t) * PROCS_MAX;
        const size_t sz = (counters_sz + page_size) & ~(page_size - 1);
	size_t i;
	uint64_t *counters;

	max_ops = args->max_ops / PROCS_MAX;

	(void)memset(pids, 0, sizeof(pids));
        counters = (uint64_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (counters == MAP_FAILED) {
                pr_fail_dbg("mmap");
                return EXIT_NO_RESOURCE;
        }

	for (i = 0; i < PROCS_MAX && keep_stressing(); i++) {
		pids[i] = fork();
		if (pids[0] < 0)
			goto reap;
		else if (pids[i] == 0) {
			/* Child */

			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			set_oom_adjustment(args->name, true);
			stress_hrtimer_process(args, &counters[i]);
			_exit(EXIT_SUCCESS);
		}
	}

	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);

		set_counter(args, 0);
		for (i = 0; i < PROCS_MAX; i++)
			add_counter(args, counters[i]);
	} while (keep_stressing());


reap:
	for (i = 0; i < PROCS_MAX; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGALRM);
	}
	for (i = 0; i < PROCS_MAX; i++) {
		if (pids[i] > 0) {
			int status, ret;

			ret = shim_waitpid(pids[i], &status, 0);
			(void)ret;
		}
	}

	(void)munmap((void *)counters, sz);

	return EXIT_SUCCESS;
}

stressor_info_t stress_hrtimers_info = {
	.stressor = stress_hrtimers,
	.class = CLASS_SCHEDULER,
	.help = help
};
#else
stressor_info_t stress_hrtimers_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER,
	.help = help
};
#endif
