/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
static uint64_t opt_itimer_freq = DEFAULT_TIMER_FREQ;
static bool set_itimer_freq = false;
static double rate_us;
static double start;

/*
 *  stress_set_itimer_freq()
 *	set itimer frequency from given option
 */
void stress_set_itimer_freq(const char *optarg)
{
	set_itimer_freq = true;
	opt_itimer_freq = get_uint64(optarg);
	check_range("itimer-freq", opt_itimer_freq,
		MIN_TIMER_FREQ, MAX_TIMER_FREQ);
}

/*
 *  stress_itimer_set()
 *	set timer, ensure it is never zero
 */
static void stress_itimer_set(struct itimerval *timer)
{
	double rate;

	if (g_opt_flags & OPT_FLAGS_TIMER_RAND) {
		/* Mix in some random variation */
		double r = ((double)(mwc32() % 10000) - 5000.0) / 40000.0;
		rate = rate_us + (rate_us * r);
	} else {
		rate = rate_us;
	}

	timer->it_value.tv_sec = (long long int)rate / 1000000;
	timer->it_value.tv_usec = (long long int)rate % 1000000;
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
static void stress_itimer_handler(int sig)
{
	struct itimerval timer;
	sigset_t mask;

	(void)sig;

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
	memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
}

/*
 *  stress_itimer
 *	stress itimer
 */
int stress_itimer(const args_t *args)
{
	struct itimerval timer;
	sigset_t mask;

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	sigprocmask(SIG_SETMASK, &mask, NULL);

	start = time_now();

	if (!set_itimer_freq) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_itimer_freq = MAX_ITIMER_FREQ;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_itimer_freq = MIN_ITIMER_FREQ;
	}
	rate_us = opt_itimer_freq ? 1000000 / opt_itimer_freq : 1000000;

	if (stress_sighandler(args->name, SIGPROF, stress_itimer_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_itimer_set(&timer);
	if (setitimer(ITIMER_PROF, &timer, NULL) < 0) {
		pr_fail_err("setitimer");
		return EXIT_FAILURE;
	}

	do {
		struct itimerval t;
		getitimer(ITIMER_PROF, &t);

		*args->counter = itimer_counter;
	} while (keep_stressing());

	memset(&timer, 0, sizeof(timer));
	(void)setitimer(ITIMER_PROF, &timer, NULL);
	return EXIT_SUCCESS;
}
