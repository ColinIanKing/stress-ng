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

#define MIN_ITIMER_FREQ		(1)
#define MAX_ITIMER_FREQ		(100000000)
#define DEFAULT_ITIMER_FREQ	(1000000)

static const stress_help_t help[] = {
	{ NULL,	"itimer N",	"start N workers exercising interval timers" },
	{ NULL,	"itimer-freq F","set the itimer frequency, limited by jiffy clock rate" },
	{ NULL,	"itimer-ops N",	"stop after N interval timer bogo operations" },
	{ NULL,	"itimer-rand",	"enable random interval timer frequency" },
	{ NULL, NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_itimer_freq, "itimer-freq", TYPE_ID_UINT64, MIN_ITIMER_FREQ, MAX_ITIMER_FREQ, NULL },
	{ OPT_itimer_rand, "itimer-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_GETITIMER) &&	\
    defined(HAVE_SETITIMER)

static stress_args_t *s_args;
static double rate_us;
static double time_end;

static const shim_itimer_which_t stress_itimers[] = {
#if defined(ITIMER_REAL)
	ITIMER_REAL,
#endif
#if defined(ITIMER_VIRTUAL)
	ITIMER_VIRTUAL,
#endif
#if defined(ITIMER_PROF)
	ITIMER_PROF,
#endif
};

/*
 *  stress_itimer_set()
 *	set timer, ensure it is never zero
 */
static void stress_itimer_set(struct itimerval *timer)
{
	double rate;
	bool itimer_rand = false;

	(void)stress_get_setting("itimer-rand", &itimer_rand);

	if (itimer_rand) {
		/* Mix in some random variation */
		double r = ((double)stress_mwc32modn(10000) - 5000.0) / 40000.0;
		rate = rate_us + (rate_us * r);
	} else {
		rate = rate_us;
	}

	timer->it_value.tv_sec = (time_t)(rate * ONE_MILLIONTH);
	timer->it_value.tv_usec = (suseconds_t)rate % 1000000;
	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_usec < 1)
		timer->it_value.tv_usec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_usec = timer->it_value.tv_usec;
}

/*
 *  stress_itimer_handler()
 *	catch itimer signal and cancel if no more runs flagged
 */
static void OPTIMIZE3 stress_itimer_handler(int sig)
{
	struct itimerval timer;
	sigset_t mask;

	(void)sig;

	if (sigpending(&mask) == 0)
		if (sigismember(&mask, SIGINT))
			goto cancel;

	if (LIKELY(!stress_continue(s_args)))
		goto cancel;
	stress_bogo_inc(s_args);
	/* High freq timer, check periodically for timeout */
	if ((stress_bogo_get(s_args) & 65535) == 0)
		if (stress_time_now() > time_end)
			goto cancel;
	return;
cancel:
	stress_continue_set_flag(false);
	/* Cancel timer if we detect no more runs */
	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
}

/*
 *  stress_itimer
 *	stress itimer
 */
static int stress_itimer(stress_args_t *args)
{
	struct itimerval timer;
	sigset_t mask;
	uint64_t itimer_freq = DEFAULT_ITIMER_FREQ;

	s_args = args;
	time_end = args->time_end;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	if (!stress_get_setting("itimer-freq", &itimer_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			itimer_freq = MAX_ITIMER_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			itimer_freq = MIN_ITIMER_FREQ;
	}
	rate_us = itimer_freq ? 1000000.0 / (double)itimer_freq : 1000000.0;

	if (stress_sighandler(args->name, SIGPROF, stress_itimer_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_itimer_set(&timer);
	if (setitimer(ITIMER_PROF, &timer, NULL) < 0) {
		if (errno == EINVAL) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: skipping stressor, setitimer with "
					"ITIMER_PROF is not implemented\n",
					args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: setitimer failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}


	do {
		size_t i;
		struct itimerval t;

		/* Get state of all itimers */
		for (i = 0; i < SIZEOF_ARRAY(stress_itimers); i++) {
			(void)getitimer(stress_itimers[i], &t);
		}
	} while (stress_continue(args));

	if (stress_bogo_get(args) == 0) {
		pr_fail("%s: did not handle any itimer SIGPROF signals\n",
			args->name);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
	return EXIT_SUCCESS;
}

const stressor_info_t stress_itimer_info = {
	.stressor = stress_itimer,
	.classifier = CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_itimer_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_INTERRUPT | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without getitimer() or setitimer() support"
};
#endif
