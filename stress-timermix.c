/*
 * Copyright (C) 2025      Colin Ian King.
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

#define DEFAULT_TIMER_FREQ	(10000000)
#define DEFAULT_ITIMER_FREQ	(100000)

#define INVALID_IDX		((size_t)-1)

#if defined(HAVE_LIB_RT) && 		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME)
#define EXERCISE_TIMER		(1)
#endif

#if defined(HAVE_GETITIMER) &&  \
    defined(HAVE_SETITIMER)
#define EXERCISE_ITIMER		(1)
#endif

static const stress_help_t help[] = {
	{ NULL, "timermix N",		"start N workers producing a mix of timer events" },
	{ NULL, "timermix-ops N",	"stop after N timer bogo events" },
	{ NULL, NULL,			NULL }
};

#if defined(EXERCISE_TIMER) ||	\
    defined(EXERCISE_ITIMER)
static stress_args_t *s_args;		/* args pointer */
static double time_end;			/* time to end timer mix stressor */
#endif

#if defined(EXERCISE_TIMER)

#define TIMER_ID_INVALID 	((timer_t)-1)

static double rate_ns;
static double timer_check;

typedef struct stress_timer_info {
	const clockid_t	clock_id;	/* clock id */
	const char 	*clock_name;	/* human readable name */
	timer_t		timer_id;	/* timer id associated with clock */
	uint64_t	count;		/* count of timer signals */
} stress_timer_info_t;

static stress_timer_info_t timer_info[] = {
#if defined(CLOCK_REALTIME)
       { CLOCK_REALTIME, 		"CLOCK_REALTIME",		0, 0 },
#endif
#if defined(CLOCK_MONOTONIC)
       { CLOCK_MONOTONIC,		"CLOCK_MONOTONIC",		0, 0 },
#endif
#if defined(CLOCK_PROCESS_CPUTIME_ID)
       { CLOCK_PROCESS_CPUTIME_ID,	"CLOCK_PROCESS_CPUTIME_ID", 	0, 0 },
#endif
#if defined(CLOCK_THREAD_CPUTIME_ID)
       { CLOCK_THREAD_CPUTIME_ID,	"CLOCK_THREAD_CPUTIME_ID", 	0, 0 },
#endif
#if defined(CLOCK_BOOTTIME)
       { CLOCK_BOOTTIME,		"CLOCK_BOOTTIME",		0, 0 },
#endif
#if defined(CLOCK_TAI)
       { CLOCK_TAI,			"CLOCK_TAI", 			0, 0 },
#endif
};

/*
 *  stress_timermix_timer_set()
 *	set timer, ensure it is never zero
 */
static inline void OPTIMIZE3 stress_timermix_timer_set(struct itimerspec *timer)
{
	timer->it_value.tv_sec = (time_t)rate_ns / STRESS_NANOSECOND;
	timer->it_value.tv_nsec = (suseconds_t)rate_ns % STRESS_NANOSECOND;
	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_nsec < 1)
		timer->it_value.tv_nsec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

static inline void stress_timermix_timer_throttle_check(const double time_now)
{
	struct itimerspec timer;
	size_t i;
	static double ok_ns = -1;

	if (time_now > timer_check + 1.0) {
		if (ok_ns < 0.0)
			rate_ns *= 2.0;
		else {
			rate_ns = ok_ns;
		}
	} else {
		//ok_ns = rate_ns;
		rate_ns = rate_ns * 0.95;
	}

	stress_timermix_timer_set(&timer);
	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		if (timer_info[i].timer_id != TIMER_ID_INVALID)
			(void)timer_settime(timer_info[i].timer_id, 0, &timer, NULL);
	}
}

/*
 *  stress_timermix_timer_action()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_timermix_timer_action(int sig, siginfo_t *siginfo, void *ucontext)
{
	struct itimerspec timer;
	sigset_t mask;
	size_t i;

	(void)sig;
	(void)ucontext;

	if (sigpending(&mask) == 0)
		if (UNLIKELY(sigismember(&mask, SIGINT)))
			goto cancel;

	if (UNLIKELY(!stress_continue(s_args)))
		goto cancel;

#if defined(__NetBSD__) ||	\
    defined(__FreeBSD__) || 	\
    defined(__sun__)
	if (LIKELY(siginfo && siginfo->si_value.sival_ptr)) {
		stress_timer_info_t *info = (stress_timer_info_t *)siginfo->si_value.sival_ptr;
		info->count++;
	}
#else

	if (LIKELY(siginfo && siginfo->si_ptr)) {
		stress_timer_info_t *info = (stress_timer_info_t *)siginfo->si_ptr;

		info->count++;
	}
#endif

	stress_bogo_inc(s_args);
	if (UNLIKELY((stress_bogo_get(s_args) & 8191) == 0)) {
		const double time_now = stress_time_now();

		if (UNLIKELY(time_now > time_end))
			goto cancel;
		stress_timermix_timer_throttle_check(time_now);
	}
	return;

cancel:
	stress_continue_set_flag(false);
	/* Cancel timer if we detect no more runs */

	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		(void)shim_memset(&timer, 0, sizeof(timer));
		(void)timer_settime(timer_info[i].timer_id, 0, &timer, NULL);
	}

}
#endif

#if defined(EXERCISE_ITIMER)
typedef struct stress_itimer_info {
	const shim_itimer_which_t itimer_id;	/* itimer id */
	const char 	*itimer_name;		/* human readable name of itimer */
	const int	signum;			/* associated signal for itimer */
	uint64_t	count;			/* count of signals handled */
} stress_itimer_info_t;

static stress_itimer_info_t itimer_info[] = {
#if defined(ITIMER_REAL) &&	\
    defined(SIGALRM)
	{ ITIMER_REAL,		"ITIMER_REAL",		SIGALRM, 0 },
#endif
#if defined(ITIMER_VIRTUAL) &&	\
    defined(SIGVTALRM)
	{ ITIMER_VIRTUAL,	"ITIMER_VIRTUAL", 	SIGVTALRM, 0 },
#endif
#if defined(ITIMER_PROF) &&	\
    defined(SIGPROF)
	{ ITIMER_PROF,		"ITIMER_PROF",		SIGPROF, 0 },
#endif
};

static double rate_us;

/*
 *  stress_timermix_itimer_set()
 *	set timer, ensure it is never zero
 */
static inline void OPTIMIZE3 stress_timermix_itimer_set(struct itimerval *timer)
{
	timer->it_value.tv_sec = (time_t)(rate_us * ONE_MILLIONTH);
	timer->it_value.tv_usec = (suseconds_t)rate_us % 1000000;

	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_usec < 1)
		timer->it_value.tv_usec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_usec = timer->it_value.tv_usec;
}

/*
 *  stress_timermix_itimer_action()
 *	catch itimer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_timermix_itimer_action(int sig, siginfo_t *siginfo, void *ucontext)
{
	struct itimerval timer;
	sigset_t mask;
	size_t i;

	(void)sig;
	(void)siginfo;
	(void)ucontext;

	if (sigpending(&mask) == 0)
		if (UNLIKELY(sigismember(&mask, SIGINT)))
			goto cancel;

	if (LIKELY(!stress_continue(s_args)))
		goto cancel;

	for (i = 0; i < SIZEOF_ARRAY(itimer_info); i++) {
		if (sig == itimer_info[i].signum) {
			itimer_info[i].count++;
			break;
		}
	}

	stress_bogo_inc(s_args);
	if (UNLIKELY((stress_bogo_get(s_args) & 31) == 0))
		if (UNLIKELY(stress_time_now() > time_end))
			goto cancel;
	return;
cancel:
	stress_continue_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)shim_memset(&timer, 0, sizeof(timer));
	for (i = 0; i < SIZEOF_ARRAY(itimer_info); i++)
		(void)setitimer(itimer_info[i].itimer_id, &timer, NULL);
}

#endif

#if defined(EXERCISE_TIMER) ||	\
    defined(EXERCISE_ITIMER)
/*
 *  stress_timermix
 *	stress timers
 */
static int stress_timermix(stress_args_t *args)
{
	struct sigaction new_action;
#if defined(EXERCISE_TIMER)
	struct sigevent sev;
	struct itimerspec timer;
#endif
#if defined(EXERCISE_ITIMER)
	struct itimerval itimer;
#endif

	int rc = EXIT_SUCCESS;
	size_t i, j;
	bool timer_created = false;
	double t_start, duration = 0.0;

	s_args = args;

	time_end = args->time_end;

#if defined(EXERCISE_TIMER)
	rate_ns = (double)STRESS_NANOSECOND / DEFAULT_TIMER_FREQ;
#endif
#if defined(EXERCISE_ITIMER)
	rate_us = (double)STRESS_MICROSECOND / DEFAULT_ITIMER_FREQ;
#endif

#if defined(EXERCISE_TIMER)
	(void)shim_memset(&new_action, 0, sizeof(new_action));
	new_action.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	new_action.sa_sigaction = stress_timermix_timer_action;
	if (sigaction(SIGRTMIN, &new_action, NULL) < 0) {
		pr_fail("%s: sigaction failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		(void)shim_memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &timer_info[i];

		if (timer_create(timer_info[i].clock_id, &sev, &timer_info[i].timer_id) < 0) {
			timer_info[i].timer_id = TIMER_ID_INVALID;
		} else  {
			timer_created = true;
		}
		timer_info[i].count = 0;
	}
#endif

#if defined(EXERCISE_ITIMER)
	(void)shim_memset(&new_action, 0, sizeof(new_action));
	new_action.sa_flags = SA_NOCLDSTOP | SA_SIGINFO;
	new_action.sa_sigaction = stress_timermix_itimer_action;
	for (i = 0; i < SIZEOF_ARRAY(itimer_info); i++) {
		if (sigaction(itimer_info[i].signum, &new_action, NULL) < 0) {
			pr_fail("%s: sigaction failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto stop_timers;
		} else {
			timer_created = true;
		}
	}
#endif

	if (!timer_created) {
		pr_inf_skip("%s: could not create any timers, out of "
			"resources, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(EXERCISE_TIMER)
	stress_timermix_timer_set(&timer);
	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		if (timer_info[i].timer_id != TIMER_ID_INVALID) {
			if (timer_settime(timer_info[i].timer_id, 0, &timer, NULL) < 0) {
				pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto stop_timers;
			}
		}
	}
#endif

#if defined(EXERCISE_ITIMER)
	stress_timermix_itimer_set(&itimer);
	for (i = 0; i < SIZEOF_ARRAY(itimer_info); i++) {
		if (setitimer(itimer_info[i].itimer_id, &itimer, NULL) < 0) {
			pr_fail("%s: setitimer failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto stop_timers;
		}
	}

#endif

	t_start = stress_time_now();
#if defined(EXERCISE_TIMER)
	timer_check = t_start;
#endif
	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 100000;
		(void)nanosleep(&req, NULL);
#if defined(EXERCISE_TIMER)
		timer_check = stress_time_now();
#endif
		shim_sched_yield();
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

stop_timers:
	j = 0;
#if defined(EXERCISE_TIMER)
	/* stop timers */
	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		(void)shim_memset(&timer, 0, sizeof(timer));
		if (timer_info[i].timer_id != TIMER_ID_INVALID)
			VOID_RET(int, timer_settime(timer_info[i].timer_id, 0, &timer, NULL));
	}

	/* stop timers */
	for (i = 0; i < SIZEOF_ARRAY(timer_info); i++) {
		if (timer_info[i].timer_id != TIMER_ID_INVALID) {
			double rate;
			char str[80];

			if (timer_delete(timer_info[i].timer_id) < 0) {
				pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}

			(void)snprintf(str, sizeof(str), "%s ticks per sec", timer_info[i].clock_name);
			rate = (duration > 0.0) ? (double)timer_info[i].count / duration : 0.0;
			stress_metrics_set(args, j, str, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
#endif

#if defined(EXERCISE_ITIMER)
	/* stop itimers */
	for (i = 0; i < SIZEOF_ARRAY(itimer_info); i++) {
		double rate;
		char str[80];

		(void)shim_memset(&itimer, 0, sizeof(itimer));
		(void)setitimer(itimer_info[i].itimer_id, &itimer, NULL);

		(void)snprintf(str, sizeof(str), "%s ticks per sec", itimer_info[i].itimer_name);
		rate = (duration > 0.0) ? (double)itimer_info[i].count / duration : 0.0;
		stress_metrics_set(args, j, str, rate, STRESS_METRIC_HARMONIC_MEAN);
		j++;
	}
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_timermix_info = {
	.stressor = stress_timermix,
	.classifier = CLASS_SIGNAL | CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_timermix_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, timer or itimer support"
};
#endif
