/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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

#include "stress-ng.h"

#if defined(STRESS_TIMERFD)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <time.h>
#include <sys/timerfd.h>

static volatile uint64_t timerfd_counter = 0;
static int timerfd;
static uint64_t opt_timerfd_freq = DEFAULT_TIMERFD_FREQ;
static bool set_timerfd_freq = false;
static double rate_ns;

/*
 *  stress_set_timerfd_freq()
 *	set timer frequency from given option
 */
void stress_set_timerfd_freq(const char *optarg)
{
	set_timerfd_freq = true;
	opt_timerfd_freq = get_uint64(optarg);
	check_range("timerfd-freq", opt_timerfd_freq,
		MIN_TIMERFD_FREQ, MAX_TIMERFD_FREQ);
}

/*
 *  stress_timerfd_set()
 *	set timerfd, ensure it is never zero
 */
static void stress_timerfd_set(struct itimerspec *timer)
{
	double rate;

	if (opt_flags & OPT_FLAGS_TIMERFD_RAND) {
		/* Mix in some random variation */
		double r = ((double)(mwc32() % 10000) - 5000.0) / 40000.0;
		rate = rate_ns + (rate_ns * r);
	} else {
		rate = rate_ns;
	}

	timer->it_value.tv_sec = (long long int)rate / 1000000000;
	timer->it_value.tv_nsec = (long long int)rate % 1000000000;

	if (timer->it_value.tv_sec == 0 &&
	    timer->it_value.tv_nsec < 1)
		timer->it_value.tv_nsec = 1;

	timer->it_interval.tv_sec = timer->it_value.tv_sec;
	timer->it_interval.tv_nsec = timer->it_value.tv_nsec;
}

/*
 *  stress_timerfd
 *	stress timerfd
 */
int stress_timerfd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	struct itimerspec timer;

	(void)instance;

	if (!set_timerfd_freq) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_timerfd_freq = MAX_TIMERFD_FREQ;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_timerfd_freq = MIN_TIMERFD_FREQ;
	}
	rate_ns = opt_timerfd_freq ? 1000000000 / opt_timerfd_freq : 1000000000;

	timerfd = timerfd_create(CLOCK_REALTIME, 0);
	if (timerfd < 0) {
		pr_fail_err(name, "timerfd_create");
		(void)close(timerfd);
		return EXIT_FAILURE;
	}
	stress_timerfd_set(&timer);
	if (timerfd_settime(timerfd, 0, &timer, NULL) < 0) {
		pr_fail_err(name, "timer_settime");
		(void)close(timerfd);
		return EXIT_FAILURE;
	}

	do {
		int ret;
		uint64_t exp;
		struct itimerspec value;
		struct timeval timeout;
		fd_set rdfs;

		FD_ZERO(&rdfs);
		FD_SET(timerfd, &rdfs);
		timeout.tv_sec = 0;
		timeout.tv_usec = 500000;

		if (!opt_do_run)
			break;
		ret = select(timerfd + 1, &rdfs, NULL, NULL, &timeout);
		if (ret < 0) {
			if (errno == EINTR)
				continue;
			pr_fail_err(name, "select");
			break;
		}
		if (ret < 1)
			continue; /* Timeout */

		ret = read(timerfd, &exp, sizeof exp);
		if (ret < 0) {
			pr_fail_err(name, "timerfd read");
			break;
		}
		if (timerfd_gettime(timerfd, &value) < 0) {
			pr_fail_err(name, "timerfd_gettime");
			break;
		}
		if (opt_flags & OPT_FLAGS_TIMERFD_RAND) {
			stress_timerfd_set(&timer);
			if (timerfd_settime(timerfd, 0, &timer, NULL) < 0) {
				pr_fail_err(name, "timer_settime");
				break;
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || timerfd_counter < max_ops));

	(void)close(timerfd);

	return EXIT_SUCCESS;
}
#endif
