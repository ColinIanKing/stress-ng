/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

#define MIN_TIMER_FREQ		(1)
#define MAX_TIMER_FREQ		(100000000)
#define DEFAULT_TIMER_FREQ	(1000000)

static const stress_help_t help[] = {
	{ "T N", "timer N",	"start N workers producing timer events" },
	{ NULL, "timer-freq F",	"run timer(s) at F Hz, range 1 to 1000000000" },
	{ NULL, "timer-ops N",	"stop after N timer bogo events" },
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

	timer_freq = stress_get_uint64(opt);
	stress_check_range("timer-freq", timer_freq,
		MIN_TIMER_FREQ, MAX_TIMER_FREQ);
	return stress_set_setting("timer-freq", TYPE_ID_UINT64, &timer_freq);
}

static int stress_set_timer_rand(const char *opt)
{
	return stress_set_setting_true("timer-rand", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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

	(void)stress_get_setting("timer-rand", &timer_rand);

	if (timer_rand) {
		/* Mix in some random variation */
		const double r = ((double)stress_mwc32modn(10000) - 5000.0) / 40000.0;
		rate = rate_ns + (rate_ns * r);
	} else {
		rate = rate_ns;
	}

	timer->it_value.tv_sec = (time_t)rate / STRESS_NANOSECOND;
	timer->it_value.tv_nsec = (suseconds_t)rate % STRESS_NANOSECOND;
	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_nsec < 1)
		timer->it_value.tv_nsec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_timer_keep_stressing(args)
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_timer_keep_stressing(void)
{
	return (LIKELY(keep_stressing_flag()) &&
		LIKELY(!max_ops || (timer_counter < max_ops)));
}

/*
 *  stress_proc_self_timer_read()
 *	exercise read of /proc/self/timers, Linux only
 */
static inline void stress_proc_self_timer_read(void)
{
#if defined(__linux__)
	char buf[1024];

	VOID_RET(ssize_t, system_read("/proc/self/timers", buf, sizeof(buf)));
#endif
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
	if ((timer_counter & 65535) == 0) {
		if ((stress_time_now() - start) > (double)g_opt_timeout)
			goto cancel;
		stress_proc_self_timer_read();
	}
	if (keep_stressing_flag()) {
		const int ret = timer_getoverrun(timerid);

		if (ret > 0)
			overruns += (uint64_t)ret;
		stress_timer_set(&timer);
		(void)timer_settime(timerid, 0, &timer, NULL);
		return;
	}

cancel:
	keep_stressing_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)memset(&timer, 0, sizeof(timer));
	(void)timer_settime(timerid, 0, &timer, NULL);
}

/*
 *  stress_timer
 *	stress timers
 */
static int stress_timer(const stress_args_t *args)
{
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	uint64_t timer_freq = DEFAULT_TIMER_FREQ;
	int n = 0;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	max_ops = args->max_ops;
	start = stress_time_now();

	if (!stress_get_setting("timer-freq", &timer_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			timer_freq = MAX_TIMER_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			timer_freq = MIN_TIMER_FREQ;
	}
	rate_ns = timer_freq ? (double)STRESS_NANOSECOND / (double)timer_freq :
			       (double)STRESS_NANOSECOND;

	if (stress_sighandler(args->name, SIGRTMIN, stress_timer_handler, NULL) < 0)
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

	stress_timer_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		struct timespec req;

		if (n++ >= 1024) {
			n = 0;

			/* Exercise nanosleep on non-permitted timespec object values */
			(void)memset(&req, 0, sizeof(req));
			req.tv_sec = -1;
			VOID_RET(int, nanosleep(&req, NULL));

			(void)memset(&req, 0, sizeof(req));
			req.tv_nsec = STRESS_NANOSECOND;
			VOID_RET(int, nanosleep(&req, NULL));
		}

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);
		set_counter(args, timer_counter);
	} while (keep_stressing(args));

	if (timer_delete(timerid) < 0) {
		pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	pr_dbg("%s: %" PRIu64 " timer overruns (instance %" PRIu32 ")\n",
		args->name, overruns, args->instance);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(__linux__)
	/* Some BSD flavours segfault on duplicated timer_delete calls */
	{
		/* Re-delete already deleted timer */
		VOID_RET(int, timer_delete(timerid));
	
		/*
		 * The manual states that EINVAL is returned when
		 * an invalid timerid is used, in practice this
		 * will most probably segfault librt, so ignore this
		 * test case for now.
		 *
		VOID_RET(int, timer_delete((timer_t)stress_mwc32()));
		 */
	}
#endif

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
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without librt, timer_create(), timer_delete(), timer_getoverrun() or timer_settime()"
};
#endif
