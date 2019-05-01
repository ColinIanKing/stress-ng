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

static volatile uint64_t itimer_counter = 0;
static uint64_t max_ops;
static double rate_us;
static double start;

static const help_t help[] = {
	{ NULL,	"itimer N",	"start N workers exercising interval timers" },
	{ NULL,	"itimer-ops N",	"stop after N interval timer bogo operations" },
	{ NULL,	"itimer-rand",	"enable random interval timer frequency" },
	{ NULL, NULL,		NULL }
};

/*
 *  stress_set_itimer_freq()
 *	set itimer frequency from given option
 */
static int stress_set_itimer_freq(const char *opt)
{
	uint64_t itimer_freq;

	itimer_freq = get_uint64(opt);
	check_range("itimer-freq", itimer_freq,
		MIN_TIMER_FREQ, MAX_TIMER_FREQ);
	return set_setting("itimer-freq", TYPE_ID_UINT64, &itimer_freq);
}

static int stress_set_itimer_rand(const char *opt)
{
	bool itimer_rand = true;

	(void)opt;
	return set_setting("itimer-rand", TYPE_ID_BOOL, &itimer_rand);
}

/*
 *  stress_itimer_set()
 *	set timer, ensure it is never zero
 */
static void stress_itimer_set(struct itimerval *timer)
{
	double rate;
	bool itimer_rand = false;

	(void)get_setting("itimer-rand", &itimer_rand);

	if (itimer_rand) {
		/* Mix in some random variation */
		double r = ((double)(mwc32() % 10000) - 5000.0) / 40000.0;
		rate = rate_us + (rate_us * r);
	} else {
		rate = rate_us;
	}

	timer->it_value.tv_sec = (time_t)rate / 1000000;
	timer->it_value.tv_usec = (suseconds_t)rate % 1000000;
	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_usec < 1)
		timer->it_value.tv_usec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_usec = timer->it_value.tv_usec;
}

/*
 *  stress_itimer_keep_stressing()
 *      returns true if we can keep on running a stressor
 */
static bool HOT OPTIMIZE3 stress_itimer_keep_stressing(void)
{
        return (LIKELY(g_keep_stressing_flag) &&
                LIKELY(!max_ops || (itimer_counter < max_ops)));
}

/*
 *  stress_itimer_handler()
 *	catch itimer signal and cancel if no more runs flagged
 */
static void stress_itimer_handler(int sig)
{
	struct itimerval timer;
	sigset_t mask;

	(void)sig;

	if (!stress_itimer_keep_stressing())
		goto cancel;
	itimer_counter++;

	if (sigpending(&mask) == 0)
		if (sigismember(&mask, SIGINT))
			goto cancel;
	/* High freq timer, check periodically for timeout */
	if ((itimer_counter & 65535) == 0)
		if ((time_now() - start) > (double)g_opt_timeout)
			goto cancel;
	if (g_keep_stressing_flag) {
		stress_itimer_set(&timer);
		return;
	}

cancel:
	g_keep_stressing_flag = false;
	/* Cancel timer if we detect no more runs */
	(void)memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
}

/*
 *  stress_itimer
 *	stress itimer
 */
static int stress_itimer(const args_t *args)
{
	struct itimerval timer;
	sigset_t mask;
	uint64_t itimer_freq = DEFAULT_TIMER_FREQ;

	(void)sigemptyset(&mask);
	(void)sigaddset(&mask, SIGINT);
	(void)sigprocmask(SIG_SETMASK, &mask, NULL);

	max_ops = args->max_ops;
	start = time_now();

	if (!get_setting("itimer-freq", &itimer_freq)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			itimer_freq = MAX_ITIMER_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			itimer_freq = MIN_ITIMER_FREQ;
	}
	rate_us = itimer_freq ? 1000000.0 / itimer_freq : 1000000.0;

	if (stress_sighandler(args->name, SIGPROF, stress_itimer_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_itimer_set(&timer);
	if (setitimer(ITIMER_PROF, &timer, NULL) < 0) {
		if (errno == EINVAL) {
			pr_inf("%s: skipping stressor, setitimer with "
				"ITIMER_PROF is not implemented\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail_err("setitimer");
		return EXIT_FAILURE;
	}

	do {
		struct itimerval t;
		(void)getitimer(ITIMER_PROF, &t);

		set_counter(args, itimer_counter);
	} while (keep_stressing());

	(void)memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_itimer_freq,	stress_set_itimer_freq },
	{ OPT_itimer_rand,	stress_set_itimer_rand },
	{ 0,			NULL }
};

stressor_info_t stress_itimer_info = {
	.stressor = stress_itimer,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
