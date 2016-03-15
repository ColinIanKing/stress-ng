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

#if defined(STRESS_PTHREAD)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <errno.h>
#include <signal.h>
#if defined(__linux__)
/* For get_robust_list on Linux: */
#include <syscall.h>
#if defined(__NR_get_robust_list)
#include <linux/futex.h>
#include <sys/types.h>
#endif
#endif

static uint64_t opt_pthread_max = DEFAULT_PTHREAD;
static bool set_pthread_max = false;
static pthread_cond_t cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static bool thread_terminate;
static uint64_t pthread_count;
static sigset_t set;

void stress_set_pthread_max(const char *optarg)
{
	set_pthread_max = true;
	opt_pthread_max = get_uint64_byte(optarg);
	check_range("pthread-max", opt_pthread_max,
		MIN_PTHREAD, MAX_PTHREAD);
}

void stress_adjust_pthread_max(uint64_t max)
{
	if (opt_pthread_max > max) {
		opt_pthread_max = max;
		pr_inf(stdout, "re-adjusting maximum threads to "
			"soft limit of %" PRIu64 "\n",
			opt_pthread_max);
	}
}

#if defined(__linux__) && defined(__NR_get_robust_list)
static inline long sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr)
{
	return syscall(__NR_get_robust_list, pid, head_ptr, len_ptr);
}
#endif

/*
 *  stress_pthread_func()
 *	pthread that exits immediately
 */
static void *stress_pthread_func(void *ctxt)
{
	uint8_t stack[SIGSTKSZ];
	stack_t ss;
	static void *nowt = NULL;
	int ret;
#if defined(__linux__) && defined(__NR_get_robust_list)
	struct robust_list_head *head_ptr;
	size_t len_ptr;
#endif

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
	 *  so this is probably just totally unncessary.
	 */
	ss.ss_sp = (void *)stack;
	ss.ss_size = SIGSTKSZ;
	ss.ss_flags = 0;
	if (sigaltstack(&ss, NULL) < 0) {
		pr_fail_err("pthread", "sigaltstack");
		goto die;
	}

#if defined(__linux__) && defined(__NR_get_robust_list)
	/*
	 *  Check that get_robust_list() works OK
	 */
	if (sys_get_robust_list(0, &head_ptr, &len_ptr) < 0) {
		if (errno != ENOSYS) {
			pr_fail_err("pthread", "get_robust_list");
			goto die;
		}
	}
#endif

	/*
	 *  Bump count of running threads
	 */
	ret = pthread_mutex_lock(&mutex);
	if (ret) {
		pr_fail_errno("pthread", "mutex lock", ret);
		goto die;
	}
	pthread_count++;
	ret = pthread_mutex_unlock(&mutex);
	if (ret) {
		pr_fail_errno("pthread", "mutex unlock", ret);
		goto die;
	}

	/*
	 *  Wait for controlling thread to
	 *  indicate it is time to die
	 */
	ret = pthread_mutex_lock(&mutex);
	if (ret) {
		pr_fail_errno("pthread", "mutex unlock", ret);
		goto die;
	}
	while (!thread_terminate) {
		ret = pthread_cond_wait(&cond, &mutex);
		if (ret) {
			pr_fail_errno("pthread",
				"pthread condition wait", ret);
			break;
		}
	}
	ret = pthread_mutex_unlock(&mutex);
	if (ret)
		pr_fail_errno("pthread", "mutex unlock", ret);
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

	if (!set_pthread_max) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_pthread_max = MAX_PTHREAD;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_pthread_max = MIN_PTHREAD;
	}

	sigfillset(&set);
	do {
		uint64_t i, j;
		int ret;

		thread_terminate = false;
		pthread_count = 0;

		for (i = 0; (i < opt_pthread_max) && (!max_ops || *counter < max_ops); i++) {
			ret = pthread_create(&pthreads[i], NULL,
				stress_pthread_func, NULL);
			if (ret) {
				/* Out of resources, don't try any more */
				if (ret == EAGAIN) {
					limited++;
					break;
				}
				/* Something really unexpected */
				pr_fail_errno(name, "pthread create", ret);
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

			ret = pthread_mutex_lock(&mutex);
			if (ret) {
				pr_fail_errno(name, "mutex lock", ret);
				ok = false;
				goto reap;
			}
			all_running = (pthread_count == i);
			ret = pthread_mutex_unlock(&mutex);
			if (ret) {
				pr_fail_errno(name, "mutex unlock", ret);
				ok = false;
				goto reap;
			}

			if (all_running)
				break;
		}

		ret = pthread_mutex_lock(&mutex);
		if (ret) {
			pr_fail_errno(name, "mutex lock", ret);
			ok = false;
			goto reap;
		}
		thread_terminate = true;
		ret = pthread_cond_broadcast(&cond);
		if (ret) {
			pr_fail_errno(name,
				"pthread condition broadcast", ret);
			ok = false;
			/* fall through and unlock */
		}
		ret = pthread_mutex_unlock(&mutex);
		if (ret) {
			pr_fail_errno(name, "mutex unlock", ret);
			ok = false;
		}
reap:
		for (j = 0; j < i; j++) {
			ret = pthread_join(pthreads[j], NULL);
			if (ret) {
				pr_fail_errno(name, "pthread join", ret);
				ok = false;
			}
		}
	} while (ok && opt_do_run && (!max_ops || *counter < max_ops));

	if (limited) {
		pr_inf(stdout, "%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			name,
			100.0 * (double)limited / (double)attempted,
			opt_pthread_max, instance);
	}

	(void)pthread_cond_destroy(&cond);
	(void)pthread_mutex_destroy(&mutex);

	return EXIT_SUCCESS;
}

#endif
