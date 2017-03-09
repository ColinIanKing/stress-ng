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

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

#include <sys/select.h>

typedef struct {
	const args_t *args;
	uint64_t counter;
	pthread_t pthread;
} ctxt_t;

static bool thread_terminate;
static sigset_t set;
#endif

static uint64_t opt_sleep_max = DEFAULT_SLEEP;
static bool set_sleep_max = false;


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
		pr_inf("re-adjusting maximum threads to "
			"soft limit f %" PRIu64 "\n",
			opt_sleep_max);
	}
}

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

/*
 *  stress_pthread_func()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_pthread_func(void *c)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	static void *nowt = NULL;
	ctxt_t *ctxt = (ctxt_t *)c;

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
	memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		goto die;

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
		if (shim_usleep(1) < 0)
			break;
		if (shim_usleep(10) < 0)
			break;
		if (shim_usleep(100) < 0)
			break;
		if (shim_usleep(1000) < 0)
			break;
		if (shim_usleep(10000) < 0)
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

		ctxt->counter++;
	}
die:
	return &nowt;
}

/*
 *  stress_sleep()
 *	stress by many sleeping threads
 */
int stress_sleep(const args_t *args)
{
	uint64_t i, n, limited = 0;
	ctxt_t    ctxts[MAX_SLEEP];
	int ret = EXIT_SUCCESS;
	bool ok = true;

	if (!set_sleep_max) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_sleep_max = MAX_SLEEP;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_sleep_max = MIN_SLEEP;
	}
	memset(ctxts, 0, sizeof(ctxts));
	sigfillset(&set);

	for (n = 0; n < opt_sleep_max;  n++) {
		ctxts[n].args = args;
		ret = pthread_create(&ctxts[n].pthread, NULL,
			stress_pthread_func, &ctxts[n]);
		if (ret) {
			/* Out of resources, don't try any more */
			if (ret == EAGAIN) {
				limited++;
				break;
			}
			/* Something really unexpected */
			pr_fail_errno("pthread create", ret);
			ret = EXIT_NO_RESOURCE;
			goto tidy;
		}
		/* Timed out? abort! */
		if (!g_keep_stressing_flag)
			goto tidy;
	}

	do {
		*args->counter = 0;
		(void)shim_usleep(10000);
		for (i = 0; i < n; i++)
			*args->counter += ctxts[i].counter;
	}  while (ok && keep_stressing());


	ret = EXIT_SUCCESS;
tidy:
	thread_terminate = true;
	for (i = 0; i < n; i++) {
		ret = pthread_join(ctxts[i].pthread, NULL);
		if (ret)
			pr_dbg("pthread join, ret=%d\b", ret);
	}

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)opt_sleep_max,
			opt_sleep_max, args->instance);
	}

	return ret;
}
#else
int stress_sleep(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
