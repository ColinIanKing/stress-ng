/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2023 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"hrtimers N",	  "start N workers that exercise high resolution timers" },
	{ NULL, "hrtimers-adjust","adjust rate to try and maximum timer rate" },
	{ NULL,	"hrtimers-ops N", "stop after N bogo high-res timer bogo operations" },
	{ NULL,	NULL,		  NULL }
};

static int stress_set_hrtimers_adjust(const char *opt)
{
        return stress_set_setting_true("hrtimers-adjust", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_hrtimers_adjust,	stress_set_hrtimers_adjust },
	{ 0,			NULL },
};

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
static volatile uint64_t *timer_counter;
static uint64_t max_ops;
static timer_t timerid;
static double start;
static long ns_delay;

#define PROCS_MAX	(32)

/*
 *  stress_hrtimers_set()
 *	set timer, ensure it is never zero
 */
static void stress_hrtimers_set(struct itimerspec *timer)
{
	if (ns_delay < 0) {
		timer->it_value.tv_nsec = (stress_mwc64() % 50000) + 1;
	} else {
		timer->it_value.tv_nsec = ns_delay;
	}
	timer->it_value.tv_sec = 0;
	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_hrtimers_keep_stressing(args)
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_hrtimers_keep_stressing(void)
{
	return (LIKELY(keep_stressing_flag()) &&
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
		if ((stress_time_now() - start) > (double)g_opt_timeout)
			goto cancel;
	stress_hrtimers_set(&timer);
	(void)timer_settime(timerid, 0, &timer, NULL);
	return;

cancel:
	keep_stressing_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
}

/*
 *  stress_hrtimer_process
 *	stress timer child process
 */
static int stress_hrtimer_process(const stress_args_t *args, uint64_t *counter)
{
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	static uint64_t last_count;
	double previous_time, dt;
	bool hrtimer_interrupt = false;

	timer_counter = counter;
	last_count = *counter;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	VOID_RET(int, stress_set_sched(getpid(), SCHED_RR, UNDEFINED, true));

	start = stress_time_now();
	if (stress_sighandler(args->name, SIGRTMIN, stress_hrtimers_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_fail("%s: timer_create failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_hrtimers_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	previous_time = stress_time_now();

	do {
		unsigned int sl_ret;

		if (ns_delay < 0) {
			sl_ret = sleep(1);
			if (sl_ret == 0)
				hrtimer_interrupt = true;
		} else {
			const long ns_adjust = ns_delay >> 2;
			double now;

			/* The sleep will be interrupted on each hrtimer tick */
			sl_ret = sleep(1);
			if (sl_ret == 0)
				hrtimer_interrupt = true;

			now = stress_time_now();
			dt = now - previous_time;

			if (dt > 0.1) {
				static double last_signal_rate = -1.0;
				double signal_rate;
				const uint64_t signal_count = *counter - last_count;

				previous_time = now;

				/* Handle unlikely time warping */
				if (UNLIKELY(dt <= 0.0))
					continue;

				signal_rate = (double)signal_count / dt;
				if (last_signal_rate > 0.0) {
					if (last_signal_rate > signal_rate) {
						ns_delay += ns_adjust;
					} else {
						ns_delay -= ns_adjust;
					}
				}
				last_signal_rate = signal_rate;
			}
		}
	} while (stress_hrtimers_keep_stressing());

	if (!hrtimer_interrupt) {
		pr_dbg("%s: failed to detect hrtimer interrupts during a 1 sec sleep\n",
			args->name);
	}

	if (timer_delete(timerid) < 0) {
		pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

static int stress_hrtimers(const stress_args_t *args)
{
	pid_t pids[PROCS_MAX];
	uint64_t *counters;
	const size_t page_size = args->page_size;
	const size_t counters_sz = sizeof(*counters) * PROCS_MAX;
	const size_t sz = (counters_sz + page_size) & ~(page_size - 1);
        bool hrtimers_adjust = false;
	double start_time, dt;
	size_t i;

        (void)stress_get_setting("hrtimers-adjust", &hrtimers_adjust);
	ns_delay = hrtimers_adjust ? 50000 : -1;

	max_ops = args->max_ops / PROCS_MAX;

	(void)memset(pids, 0, sizeof(pids));
	counters = (uint64_t *)mmap(NULL, sz, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (counters == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zd bytes, errno=%d (%s), "
			"skipping stressor\n",
			args->name, sz, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < PROCS_MAX; i++) {
		if (!keep_stressing(args))
			goto reap;

		pids[i] = fork();
		if (pids[i] == 0) {
			/* Child */

			stress_parent_died_alarm();
			stress_set_oom_adjustment(args->name, true);
			(void)sched_settings_apply(true);
			stress_hrtimer_process(args, &counters[i]);
			_exit(EXIT_SUCCESS);
		}
	}

	start_time = stress_time_now();

	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);

		set_counter(args, 0);
		for (i = 0; i < PROCS_MAX; i++)
			add_counter(args, counters[i]);
	} while (keep_stressing(args));

	dt = stress_time_now() - start_time;
	if (dt > 0.0) {
		const uint64_t c = get_counter(args);

		pr_inf("%s: hrtimer signals at %.3f MHz\n",
			args->name, ((double)c / dt) / 1000000.0);
	}


reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < PROCS_MAX; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGALRM);
	}
	for (i = 0; i < PROCS_MAX; i++) {
		if (pids[i] > 0) {
			int status;

			VOID_RET(int, shim_waitpid(pids[i], &status, 0));
		}
	}

	(void)munmap((void *)counters, sz);

	return EXIT_SUCCESS;
}

stressor_info_t stress_hrtimers_info = {
	.stressor = stress_hrtimers,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_hrtimers_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without librt or hrtimer support"
};
#endif
