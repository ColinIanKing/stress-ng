/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-builtin.h"

#include <time.h>

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
static stress_args_t *s_args;
static volatile uint64_t timer_settime_failure;
static uint64_t timer_overruns;
static timer_t timerid;
static double rate_ns;
static double time_end;
static bool timer_rand;
#endif

static const stress_opt_t opts[] = {
	{ OPT_timer_freq, "timer-freq", TYPE_ID_UINT64, MIN_TIMER_FREQ, MAX_TIMER_FREQ, NULL },
	{ OPT_timer_rand, "timer-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
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
static void OPTIMIZE3 stress_timer_set(struct itimerspec *timer)
{
	double rate;

	if (UNLIKELY(timer_rand)) {
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
 *  stress_proc_self_timer_read()
 *	exercise read of /proc/self/timers, Linux only
 */
static inline void stress_proc_self_timer_read(void)
{
#if defined(__linux__)
	(void)stress_system_discard("/proc/self/timers");
#endif
}

/*
 *  stress_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_timer_handler(int sig)
{
	struct itimerspec timer;
	sigset_t mask;
	int ret;

	(void)sig;

	if (sigpending(&mask) == 0)
		if (sigismember(&mask, SIGINT))
			goto cancel;
	/* High freq timer, check periodically for timeout */
	if (UNLIKELY(!stress_continue(s_args)))
		goto cancel;
	stress_bogo_inc(s_args);
	if (UNLIKELY((stress_bogo_get(s_args) & 65535) == 0)) {
		if (stress_time_now() > time_end)
			goto cancel;
		stress_proc_self_timer_read();
	}
	ret = timer_getoverrun(timerid);
	if (ret > 0)
		timer_overruns += (uint64_t)ret;

	return;

cancel:
	stress_continue_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)shim_memset(&timer, 0, sizeof(timer));
	if (UNLIKELY(timer_settime(timerid, 0, &timer, NULL) < 0))
		timer_settime_failure++;
}

/*
 *  stress_timer
 *	stress timers
 */
static int stress_timer(stress_args_t *args)
{
	struct sigevent sev;
	struct itimerspec timer;
	sigset_t mask;
	uint64_t timer_freq = DEFAULT_TIMER_FREQ;
	int n = 0, rc = EXIT_SUCCESS;

	s_args = args;

	time_end = args->time_end;
	timer_settime_failure = 0;
	timer_overruns = 0;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	timer_rand = false;
	(void)stress_get_setting("timer-rand", &timer_rand);
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

	(void)shim_memset(&sev, 0, sizeof(sev));
	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM) || (errno == ENOTSUP)) {
			pr_inf_skip("%s: could not create timer, out of "
				"resources, skipping stressor\n",
				args->name);
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: timer_create failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_timer_set(&timer);
	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	do {
		struct timespec req;

		if (UNLIKELY(n++ >= 1024)) {
			n = 0;

			/* Exercise nanosleep on non-permitted timespec object values */
			(void)shim_memset(&req, 0, sizeof(req));
			req.tv_sec = -1;
			VOID_RET(int, nanosleep(&req, NULL));

			(void)shim_memset(&req, 0, sizeof(req));
			req.tv_nsec = STRESS_NANOSECOND;
			VOID_RET(int, nanosleep(&req, NULL));

			if (UNLIKELY(timer_rand)) {
				(void)shim_memset(&timer, 0, sizeof(timer));
				if (UNLIKELY(timer_settime(timerid, 0, &timer, NULL) < 0))
					timer_settime_failure++;
				stress_timer_set(&timer);
				if (UNLIKELY(timer_settime(timerid, 0, &timer, NULL) < 0))
					timer_settime_failure++;
			}
		}

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);
	} while (stress_continue(args));

	/* stop timer */
	(void)shim_memset(&timer, 0, sizeof(timer));
	VOID_RET(int, timer_settime(timerid, 0, &timer, NULL));

	if (timer_delete(timerid) < 0) {
		pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	}
	pr_dbg("%s: %" PRIu64 " timer overruns (instance %" PRIu32 ")\n",
		args->name, timer_overruns, args->instance);

	if (timer_settime_failure) {
		pr_fail("%s: %" PRIu64 " timer settime calls failed\n",
			args->name, timer_settime_failure);
		rc = EXIT_FAILURE;
	}

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

	return rc;
}

const stressor_info_t stress_timer_info = {
	.stressor = stress_timer,
	.classifier = CLASS_SIGNAL | CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_timer_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, timer_create(), timer_delete(), timer_getoverrun() or timer_settime()"
};
#endif
