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

#if defined(STRESS_SLEEP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#include <time.h>

static uint64_t opt_sleep_max = DEFAULT_SLEEP;
static bool set_sleep_max = false;
static bool thread_terminate;
static sigset_t set;

void stress_set_sleep_max(const char *optarg)
{
	set_sleep_max = true;
	opt_sleep_max = get_uint64_byte(optarg);
	check_range("sleep-max", opt_sleep_max,
		MIN_SLEEP, MAX_SLEEP);
}

void stress_adjust_sleep_max(uint64_t max)
{
	if (opt_sleep_max > max) {
		opt_sleep_max = max;
		pr_inf(stdout, "re-adjusting maximum threads to "
			"soft limit f %" PRIu64 "\n",
			opt_sleep_max);
	}
}

/*
 *  stress_pthread_func()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_pthread_func(void *ctxt)
{
	uint8_t stack[SIGSTKSZ];
	stack_t ss;
	static void *nowt = NULL;
	uint64_t *counter = (uint64_t *)ctxt;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err("pthread", "sigaltstack");
		goto die;
	}

	while (!thread_terminate) {
		struct timespec tv;
		struct timeval timeout;

		tv.tv_sec = 0;
		tv.tv_nsec = 1;
		if (nanosleep(&tv, NULL) < 0)
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 10;
		if (nanosleep(&tv, NULL) < 0)
			break;
		tv.tv_sec = 0;
		tv.tv_nsec = 100;
		if (nanosleep(&tv, NULL) < 0)
			break;
		if (usleep(1) < 0)
			break;
		if (usleep(10) < 0)
			break;
		if (usleep(100) < 0)
			break;
		if (usleep(1000) < 0)
			break;
		if (usleep(10000) < 0)
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10;
		if (select(0, NULL, NULL, NULL, &timeout) < 0)
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 100;
		if (select(0, NULL, NULL, NULL, &timeout) < 0)
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 1000;
		if (select(0, NULL, NULL, NULL, &timeout) < 0)
			break;
		timeout.tv_sec = 0;
		timeout.tv_usec = 10000;
		if (select(0, NULL, NULL, NULL, &timeout) < 0)
			break;

		(*counter)++;
	}
die:
	return &nowt;
}

/*
 *  stress_sleep()
 *	stress by many sleeping threads
 */
int stress_sleep(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint64_t i, n, c, limited = 0;
	uint64_t  counters[MAX_SLEEP];
	pthread_t pthreads[MAX_SLEEP];
	int ret = EXIT_SUCCESS;
	bool ok = true;

	if (!set_sleep_max) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_sleep_max = MAX_SLEEP;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_sleep_max = MIN_SLEEP;
	}
	memset(pthreads, 0, sizeof(pthreads));
	memset(counters, 0, sizeof(counters));
	sigfillset(&set);

	for (n = 0; n < opt_sleep_max;  n++) {
		ret = pthread_create(&pthreads[n], NULL,
			stress_pthread_func, &counters[n]);
		if (ret) {
			/* Out of resources, don't try any more */
			if (ret == EAGAIN) {
				limited++;
				break;
			}
			/* Something really unexpected */
			pr_fail_errno(name, "pthread create", ret);
			ret = EXIT_NO_RESOURCE;
			goto tidy;
		}
		/* Timed out? abort! */
		if (!opt_do_run)
			goto tidy;
	}

	do {
		c = 0;
		usleep(10000);
		for (i = 0; i < n; i++)
			c += counters[i];
	}  while (ok && opt_do_run && (!max_ops || c < max_ops));

	*counter = c;

	ret = EXIT_SUCCESS;
tidy:
	thread_terminate = true;
	for (i = 0; i < n; i++) {
		ret = pthread_join(pthreads[i], NULL);
		if (ret)
			pr_dbg(stderr, "pthread join, ret=%d", ret);
	}

	if (limited) {
		pr_inf(stdout, "%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			name,
			100.0 * (double)limited / (double)opt_sleep_max,
			opt_sleep_max, instance);
	}

	return ret;
}

#endif
