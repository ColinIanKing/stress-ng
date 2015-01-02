/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>

#include "stress-ng.h"

#if defined (__linux__)
static volatile uint64_t timer_counter = 0;
static timer_t timerid;
static uint64_t opt_timer_freq;

/*
 *  stress_set_timer_freq()
 *	set timer frequency from given option
 */
void stress_set_timer_freq(const char *optarg)
{
	opt_timer_freq = get_uint64(optarg);
	check_range("timer-freq", opt_timer_freq,
		MIN_TIMER_FREQ, MAX_TIMER_FREQ);
}

/*
 *  stress_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void stress_timer_handler(int sig)
{
	(void)sig;
	timer_counter++;

	/* Cancel timer if we detect no more runs */
	if (!opt_do_run) {
		struct itimerspec timer;

		timer.it_value.tv_sec = 0;
		timer.it_value.tv_nsec = 0;
		timer.it_interval.tv_sec = timer.it_value.tv_sec;
		timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

		timer_settime(timerid, 0, &timer, NULL);
	}
}

/*
 *  stress_timer
 *	stress timers
 */
int stress_timer(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	struct sigaction new_action;
	struct sigevent sev;
	struct itimerspec timer;
	const double rate_ns = opt_timer_freq ? 1000000000 / opt_timer_freq : 1000000000;

	(void)instance;

	new_action.sa_flags = 0;
	new_action.sa_handler = stress_timer_handler;
	sigemptyset(&new_action.sa_mask);
	if (sigaction(SIGRTMIN, &new_action, NULL) < 0) {
		pr_failed_err(name, "sigaction");
		return EXIT_FAILURE;
	}

	sev.sigev_notify = SIGEV_SIGNAL;
	sev.sigev_signo = SIGRTMIN;
	sev.sigev_value.sival_ptr = &timerid;
	if (timer_create(CLOCK_REALTIME, &sev, &timerid) < 0) {
		pr_failed_err(name, "timer_create");
		return EXIT_FAILURE;
	}

	timer.it_value.tv_sec = (long long int)rate_ns / 1000000000;
	timer.it_value.tv_nsec = (long long int)rate_ns % 1000000000;
	timer.it_interval.tv_sec = timer.it_value.tv_sec;
	timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

	if (timer_settime(timerid, 0, &timer, NULL) < 0) {
		pr_failed_err(name, "timer_settime");
		return EXIT_FAILURE;
	}

	do {
		struct timespec req;

		req.tv_sec = 0;
		req.tv_nsec = 10000000;
		(void)nanosleep(&req, NULL);
		*counter = timer_counter;
	} while (opt_do_run && (!max_ops || timer_counter < max_ops));

	if (timer_delete(timerid) < 0) {
		pr_failed_err(name, "timer_delete");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
#endif
