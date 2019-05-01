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
	{ "T N", "timer N",	"start N workers producing timer events" },
	{ NULL, "timer-ops N",	"stop after N timer bogo events" },
	{ NULL, "timer-freq F",	"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL, "timer-rand",	"enable random timer frequency" },
	{ NULL, NULL,		NULL }
};

#if defined(HAVE_LIB_RT) && 		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME)
static volatile uint64_t timer_counter = 0;
static uint64_t max_ops;
static timer_t timerid;
static uint64_t overruns = 0;
static double rate_ns;
static double start;
#endif

/*
 *  stress_set_timer_freq()
 *	set timer frequency from given option
 */
static int stress_set_timer_freq(const char *opt)
{
	uint64_t timer_freq;

	timer_freq = get_uint64(opt);
	check_range("timer-freq", timer_freq,
		MIN_TIMER_FREQ, MAX_TIMER_FREQ);
	return set_setting("timer-freq", TYPE_ID_UINT64, &timer_freq);
}

static int stress_set_timer_rand(const char *opt)
{
	bool timer_rand = true;

	(void)opt;
	return set_setting("timer-rand", TYPE_ID_BOOL, &timer_rand);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_timer_freq,	stress_set_timer_freq },
	{ OPT_timer_rand,	stress_set_timer_rand },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_GETOVERRUN) &&	\
    defined(HAVE_TIMER_SETTIME)

/*
 *  stress_timer_set()
 *	set timer, ensure it is never zero
 */
static void stress_timer_set(struct itimerspec *timer)
{
	double rate;
	bool timer_rand = false;

	(void)get_setting("timer-rand", &timer_rand);

	if (timer_rand) {
		/* Mix in some random variation */
		double r = ((double)(mwc32() % 10000) - 5000.0) / 40000.0;
		rate = rate_ns + (rate_ns * r);
	} else {
		rate = rate_ns;
	}

	timer->it_value.tv_sec = (time_t)rate / 1000000000;
	timer->it_value.tv_nsec = (suseconds_t)rate % 1000000000;
	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_nsec < 1)
		timer->it_value.tv_nsec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_timer_keep_stressing()
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_timer_keep_stressing(void)
{
        return (LIKELY(g_keep_stressing_flag) &&
                LIKELY(!max_ops || (timer_counter < max_ops)));
}

/*
 *  stress_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT stress_timer_handler(int sig)
{
	struct itimerspec timer;
	sigset_t mask;

	(void)sig;

	if (!stress_timer_keep_stressing())
		goto cancel;
	timer_counter++;

	if (sigpending(&mask) == 0)
		if (sigismember(&mask, SIGINT))
			goto cancel;
	/* High freq timer, check periodically for timeout */
	if ((timer_counter & 65535) == 0)
		if ((time_now() - start) > (double)g_opt_timeout)
			goto cancel;
	if (g_keep_stressing_flag) {
		int ret = timer_getoverrun(timerid);
		if (ret > 0)
			overruns += ret;
		stress_timer_set(&timer);
		return;
	}

cancel:
	g_keep_stressing_flag = false;
	/* Cancel timer if we detect no more runs */
	(void)memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
}

/*
 *  stress_timer
 *	stress timers
 */
static int stress_timer(const args_t *args)
{
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	uint64_t timer_freq = DEFAULT_TIMER_FREQ;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	max_ops = args->max_ops;
	start = time_now();

	if (!get_setting("timer-freq", &timer_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			timer_freq = MAX_TIMER_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			timer_freq = MIN_TIMER_FREQ;
	}
	rate_ns = timer_freq ? 1000000000.0 / timer_freq : 1000000000.0;

	if (stress_sighandler(args->name, SIGRTMIN, stress_timer_handler, NULL) < 0)
		return EXIT_FAILURE;

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_fail_err("timer_create");
		return EXIT_FAILURE;
	}

	stress_timer_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail_err("timer_settime");
		return EXIT_FAILURE;
	}

	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);
		set_counter(args, timer_counter);
	} while (keep_stressing());

	if (timer_delete(timerid) < 0) {
		pr_fail_err("timer_delete");
		return EXIT_FAILURE;
	}
	pr_dbg("%s: %" PRIu64 " timer overruns (instance %" PRIu32 ")\n",
		args->name, overruns, args->instance);

	return EXIT_SUCCESS;
}

stressor_info_t stress_timer_info = {
	.stressor = stress_timer,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_timer_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
