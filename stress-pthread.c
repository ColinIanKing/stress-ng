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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>

#include "stress-ng.h"

static uint64_t opt_pthread_max = DEFAULT_PTHREAD;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static bool thread_terminate;
static uint64_t pthread_count;
static sigset_t set;

void stress_set_pthread_max(const char *optarg)
{
	opt_pthread_max = get_uint64_byte(optarg);
	check_range("pthread-max", opt_pthread_max,
		MIN_PTHREAD, MAX_PTHREAD);
}

void stress_adjust_ptread_max(uint64_t max)
{
	if (opt_pthread_max > max) {
		opt_pthread_max = max;
		pr_inf(stdout, "re-adjusting maximum threads to "
			"soft limit of %" PRIu64 "\n",
			opt_pthread_max);
	}
}

/*
 *  stress_pthread_func()
 *	pthread that exits immediately
 */
void *stress_pthread_func(void *ctxt)
{
	uint8_t stack[SIGSTKSZ];
	stack_t ss;
	static void *nowt = NULL;

	(void)ctxt;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totall unncessary.
	 */
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_failed_err("pthread", "sigaltstack");
		goto die;
	}

	/*
	 *  Bump count of running threads
	 */
	if (pthread_mutex_lock(&mutex)) {
		pr_failed_err("pthread", "mutex lock");
		goto die;
	}
	pthread_count++;
	if (pthread_mutex_unlock(&mutex)) {
		pr_failed_err("pthread", "mutex unlock");
		goto die;
	}

	/*
	 *  Wait for controlling thread to
	 *  indicate it is time to die
	 */
	if (pthread_mutex_lock(&mutex)) {
		pr_failed_err("pthread", "mutex unlock");
		goto die;
	}
	while (!thread_terminate) {
		if (pthread_cond_wait(&cond, &mutex)) {
			pr_failed_err("pthread", "pthread condition wait");
			break;
		}
	}
	if (pthread_mutex_unlock(&mutex)) {
		pr_failed_err("pthread", "mutex unlock");
	}
die:
	return &nowt;
}

/*
 *  stress_pthread()
 *	stress by creating pthreads
 */
int stress_pthread(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pthread_t pthreads[MAX_PTHREAD];
	bool ok = true;
	uint64_t limited = 0, attempted = 0;

	(void)instance;

	sigfillset(&set);
	do {
		uint64_t i, j;

		thread_terminate = false;
		pthread_count = 0;

		for (i = 0; (i < opt_pthread_max) && (!max_ops || *counter < max_ops); i++) {
			if (pthread_create(&pthreads[i], NULL, stress_pthread_func, NULL)) {
				/* Out of resources, don't try any more */
				if (errno == EAGAIN) {
					limited++;
					break;
				}
				/* Something really unexpected */
				pr_failed_err(name, "pthread create");
				ok = false;
				break;
			}
			(*counter)++;
			if (!opt_do_run)
				break;
		}
		attempted++;

		/*
		 *  Wait until they are all started or
		 *  we get bored waiting..
		 */
		for (j = 0; j < 1000; j++) {
			bool all_running = false;

			if (pthread_mutex_lock(&mutex)) {
				pr_failed_err(name, "mutex lock");
				ok = false;
				goto reap;
			}
			all_running = (pthread_count == i);
			if (pthread_mutex_unlock(&mutex)) {
				pr_failed_err(name, "mutex unlock");
				ok = false;
				goto reap;
			}

			if (all_running)
				break;
		}

		if (pthread_mutex_lock(&mutex)) {
			pr_failed_err(name, "mutex lock");
			ok = false;
			goto reap;
		}
		thread_terminate = true;
		if (pthread_cond_broadcast(&cond)) {
			pr_failed_err(name, "pthread condition broadcast");
			ok = false;
			/* fall through and unlock */
		}
		if (pthread_mutex_unlock(&mutex)) {
			pr_failed_err(name, "mutex unlock");
			ok = false;
		}
reap:
		for (j = 0; j < i; j++) {
			if (pthread_join(pthreads[j], NULL)) {
				pr_failed_err(name, "pthread join");
				ok = false;
			}
		}
	} while (ok && opt_do_run && (!max_ops || *counter < max_ops));

	if (limited) {
		pr_inf(stdout, "%.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			100.0 * (double)limited / (double)attempted,
			opt_pthread_max, instance);
	}

	(void)pthread_cond_destroy(&cond);
	(void)pthread_mutex_destroy(&mutex);

	return EXIT_SUCCESS;
}
