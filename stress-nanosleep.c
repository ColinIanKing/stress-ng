/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

#define MAX_NANOSLEEP_THREADS	(8)

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_NANOSLEEP)

typedef struct {
	const stress_args_t *args;
	uint64_t counter;
	pthread_t pthread;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	double overrun_nsec;
	double count;
#endif
} stress_ctxt_t;

static volatile bool thread_terminate;
static sigset_t set;
#endif

static const stress_help_t help[] = {
	{ NULL,	"nanosleep N",		"start N workers performing short sleeps" },
	{ NULL,	"nanosleep-ops N",	"stop after N bogo sleep operations" },
	{ NULL,	NULL,			NULL }
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
	static void *nowt = NULL;
	stress_ctxt_t *ctxt = (stress_ctxt_t *)c;
	const stress_args_t *args = ctxt->args;
	const uint64_t max_ops =
		args->max_ops ? (args->max_ops / MAX_NANOSLEEP_THREADS) + 1 : 0;

	while (keep_stressing(args) &&
	       !thread_terminate &&
	       (!max_ops || (ctxt->counter < max_ops))) {
		unsigned long i;

		for (i = 1 << 18; i > 0; i >>=1) {
			struct timespec tv;
			long nsec = (long)(stress_mwc32() % i) + 8;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
			struct timespec t1;

			tv.tv_sec = 0;
			tv.tv_nsec = nsec;

			(void)clock_gettime(CLOCK_MONOTONIC, &t1);
			if (LIKELY(nanosleep(&tv, NULL) == 0)) {
				struct timespec t2;
				long dt_nsec;

				(void)clock_gettime(CLOCK_MONOTONIC, &t2);

				dt_nsec = (t2.tv_sec - t1.tv_sec) * 1000000000;
				dt_nsec += t2.tv_nsec - t1.tv_nsec;
				dt_nsec -= nsec;
				ctxt->overrun_nsec += (double)dt_nsec;
				ctxt->count += 1.0;
			} else {
				break;
			}
#else
			tv.tv_sec = 0;
			tv.tv_nsec = nsec;

			if (UNLIKELY(nanosleep(&tv, NULL) < 0))
				break;
#endif
		}
		ctxt->counter++;
	}
	return &nowt;
}

/*
 *  stress_nanosleep()
 *	stress nanosleep by many sleeping threads
 */
static int stress_nanosleep(const stress_args_t *args)
{
	uint64_t i, n, limited = 0;
	static stress_ctxt_t ctxts[MAX_NANOSLEEP_THREADS];
	int ret = EXIT_SUCCESS;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	double overhead_nsec, overrun_nsec, count;
	const uint64_t benchmark_loops = 10000;
#endif

	if (stress_sighandler(args->name, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)memset(ctxts, 0, sizeof(ctxts));
	(void)sigfillset(&set);

	for (n = 0; n < MAX_NANOSLEEP_THREADS; n++) {
		ctxts[n].counter = 0;
		ctxts[n].args = args;
#if defined(HAVE_CLOCK_GETTIME) && 	\
    defined(CLOCK_MONOTONIC)
		ctxts[n].overrun_nsec = 0.0;
		ctxts[n].count = 0.0;
#endif
		ret = pthread_create(&ctxts[n].pthread, NULL,
			stress_pthread_func, &ctxts[n]);
		if (ret) {
			/* Out of resources, don't try any more */
			if (ret == EAGAIN) {
				limited++;
				break;
			}
			/* Something really unexpected */
			pr_err("%s: pthread create failed, errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			ret = EXIT_NO_RESOURCE;
			goto tidy;
		}
		/* Timed out? abort! */
		if (!keep_stressing_flag())
			goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		set_counter(args, 0);
		(void)shim_usleep_interruptible(10000);
		for (i = 0; i < n; i++)
			add_counter(args, ctxts[i].counter);
	}  while (!thread_terminate && keep_stressing(args));

	ret = EXIT_SUCCESS;


tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	thread_terminate = true;
	for (i = 0; i < n; i++) {
		VOID_RET(int, pthread_join(ctxts[i].pthread, NULL));
	}

#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	overhead_nsec = 0.0;
	for (i = 0; i < benchmark_loops; i++) {
		struct timespec t1, t2;
		long dt_nsec;

		(void)clock_gettime(CLOCK_MONOTONIC, &t1);
		(void)clock_gettime(CLOCK_MONOTONIC, &t2);

		dt_nsec = (t2.tv_sec - t1.tv_sec) * 1000000000;
		dt_nsec += t2.tv_nsec - t1.tv_nsec;

		overhead_nsec += dt_nsec;
	}
	overhead_nsec /= (double)benchmark_loops;

	count = 0.0;
	overrun_nsec = 0.0;
	for (i = 0; i < n; i++) {
		overrun_nsec += ctxts[i].overrun_nsec;
		count += (double)ctxts[i].count;
	}
	overrun_nsec -= overhead_nsec;
	overrun_nsec = (count > 0.0) ? overrun_nsec / count : 0.0;
	stress_misc_stats_set(args->misc_stats, 2, "nanosec sleep overrun", overrun_nsec);
#endif

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %d threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)MAX_NANOSLEEP_THREADS,
			MAX_NANOSLEEP_THREADS, args->instance);
	}

	return ret;
}

stressor_info_t stress_nanosleep_info = {
	.stressor = stress_nanosleep,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_nanosleep_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without librt or nanosleep() system call support"
};
#endif
