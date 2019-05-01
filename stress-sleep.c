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

#if defined(HAVE_LIB_PTHREAD)

typedef struct {
	const args_t *args;
	uint64_t counter;
	uint64_t sleep_max;
	pthread_t pthread;
} ctxt_t;

static volatile bool thread_terminate;
static sigset_t set;
#endif

static const help_t help[] = {
	{ NULL,	"sleep N",	"start N workers performing various duration sleeps" },
	{ NULL,	"sleep-ops N",	"stop after N bogo sleep operations" },
	{ NULL,	"sleep-max P",	"create P threads at a time by each worker" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_sleep_max(const char *opt)
{
	uint64_t sleep_max;

	sleep_max = get_uint64(opt);
	check_range("sleep-max", sleep_max,
		MIN_SLEEP, MAX_SLEEP);
	return set_setting("sleep-max", TYPE_ID_UINT64, &sleep_max);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sleep_max,	stress_set_sleep_max },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_PTHREAD)

static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
        (void)signum;

        thread_terminate = true;
}

/*
 *  stress_pthread_func()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_pthread_func(void *c)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	static void *nowt = NULL;
	ctxt_t *ctxt = (ctxt_t *)c;
	const args_t *args = ctxt->args;
	const uint64_t max_ops = 
		args->max_ops ? (args->max_ops / ctxt->sleep_max) + 1 : 0;
	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		goto die;

	while (keep_stressing() && 
	       !thread_terminate &&
               (!max_ops || ctxt->counter < max_ops)) {
		struct timespec tv;
#if defined(HAVE_SYS_SELECT_H)
		struct timeval timeout;
#endif

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

#if defined(HAVE_SYS_SELECT_H)
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
#endif

		ctxt->counter++;
	}
die:
	return &nowt;
}

/*
 *  stress_sleep()
 *	stress by many sleeping threads
 */
static int stress_sleep(const args_t *args)
{
	uint64_t i, n, limited = 0;
	uint64_t sleep_max = DEFAULT_SLEEP;
	static ctxt_t ctxts[MAX_SLEEP];
	int ret = EXIT_SUCCESS;

	if (!get_setting("sleep-max", &sleep_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sleep_max = MAX_SLEEP;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sleep_max = MIN_SLEEP;
	}

	if (stress_sighandler(args->name, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)memset(ctxts, 0, sizeof(ctxts));
	(void)sigfillset(&set);

	for (n = 0; n < sleep_max;  n++) {
		ctxts[n].args = args;
		ctxts[n].sleep_max = sleep_max;
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
		set_counter(args, 0);
		(void)shim_usleep_interruptible(10000);
		for (i = 0; i < n; i++)
			add_counter(args, ctxts[i].counter);
	}  while (!thread_terminate && keep_stressing());

	ret = EXIT_SUCCESS;
tidy:
	thread_terminate = true;
	for (i = 0; i < n; i++) {
		ret = pthread_join(ctxts[i].pthread, NULL);
		(void)ret;
		/*
		if (ret)
			pr_dbg("%s: pthread join, ret=%d\n", args->name, ret);
		*/
	}

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)sleep_max,
			sleep_max, args->instance);
	}

	return ret;
}

stressor_info_t stress_sleep_info = {
	.stressor = stress_sleep,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sleep_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
