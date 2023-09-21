// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-cpuidle.h"

#define MIN_NANOSLEEP_THREADS		(1)
#define MAX_NANOSLEEP_THREADS		(1024)
#define DEFAULT_NANOSLEEP_THREADS	(8)

#if defined(HAVE_LIB_PTHREAD) &&	\
    defined(HAVE_NANOSLEEP)

typedef struct {
	const stress_args_t *args;
	cpu_cstate_t *cstate_list;
	uint64_t counter;
	uint64_t max_ops;
	pthread_t pthread;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	double overrun_nsec;
	double overrun_count;
	double underrun_nsec;
	double underrun_count;
#endif
	int mask;
} stress_ctxt_t;

#define STRESS_NANOSLEEP_CSTATE		(0x01)
#define STRESS_NANOSLEEP_RANDOM		(0x02)
#define STRESS_NANOSLEEP_NS		(0x04)
#define STRESS_NANOSLEEP_US		(0x08)
#define STRESS_NANOSLEEP_MS		(0x10)
#define STRESS_NANOSLEEP_ALL		(STRESS_NANOSLEEP_CSTATE |	\
					 STRESS_NANOSLEEP_RANDOM |	\
					 STRESS_NANOSLEEP_NS | 		\
					 STRESS_NANOSLEEP_US |		\
					 STRESS_NANOSLEEP_MS)
typedef struct {
	const char *name;
	int mask;
} stress_nanosleep_method_t;

static volatile bool thread_terminate;
static sigset_t set;
#endif

static const stress_help_t help[] = {
	{ NULL,	"nanosleep N",		"start N workers performing short sleeps" },
	{ NULL,	"nanosleep-ops N",	"stop after N bogo sleep operations" },
	{ NULL,	"nanosleep-threads N",	"number of threads to run concurrently (default 8)" },
	{ NULL,	"nanosleep-method M",	"select nanosleep sleep time method [ all | cstate | random ]" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_nanosleep_threads(const char *opt)
{
	uint32_t nanosleep_threads;

	nanosleep_threads = stress_get_uint32(opt);
	stress_check_range("nanosleep_threads", nanosleep_threads,
		MIN_NANOSLEEP_THREADS, MAX_NANOSLEEP_THREADS);
	return stress_set_setting("nanosleep-threads", TYPE_ID_UINT32, &nanosleep_threads);
}

static const stress_nanosleep_method_t stress_nanosleep_methods[] = {
	{ "all",	STRESS_NANOSLEEP_ALL },
	{ "cstate",	STRESS_NANOSLEEP_CSTATE },
	{ "random",	STRESS_NANOSLEEP_RANDOM },
	{ "ns",		STRESS_NANOSLEEP_NS },
	{ "us",		STRESS_NANOSLEEP_US },
	{ "ms",		STRESS_NANOSLEEP_MS },
};

/*
 *  stress_set_nanosleep_method()
 *	set the default nanosleep time method
 */
static int stress_set_nanosleep_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_nanosleep_methods); i++) {
		if (!strcmp(stress_nanosleep_methods[i].name, name)) {
			stress_set_setting("stress-nanosleep-method", TYPE_ID_INT, &stress_nanosleep_methods[i].mask);
			return 0;
		}
	}

	(void)fprintf(stderr, "nanosleep-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_nanosleep_methods); i++) {
		(void)fprintf(stderr, " %s", stress_nanosleep_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_nanosleep_threads,	stress_set_nanosleep_threads },
	{ OPT_nanosleep_method,		stress_set_nanosleep_method },
	{ 0,				NULL }
};

#if defined(HAVE_LIB_PTHREAD)

static void MLOCKED_TEXT stress_sigalrm_handler(int signum)
{
	(void)signum;

	thread_terminate = true;
}

/*
 *  stress_nanosleep_ns()
 *	nanosleep for a given number of nanoseconds
 */
static int stress_nanosleep_ns(stress_ctxt_t *ctxt, const long nsec)
{
	struct timespec tv;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	struct timespec t1;

	tv.tv_sec = 0;
	tv.tv_nsec = nsec;

	(void)clock_gettime(CLOCK_MONOTONIC, &t1);
	if (LIKELY(nanosleep(&tv, NULL) == 0)) {
		struct timespec t2;

		if (clock_gettime(CLOCK_MONOTONIC, &t2) == 0) {
			long dt_nsec;

			dt_nsec = (t2.tv_sec - t1.tv_sec) * 1000000000;
			dt_nsec += t2.tv_nsec - t1.tv_nsec;
			dt_nsec -= nsec;

			if (dt_nsec < 0) {
				ctxt->underrun_nsec += (double)labs(dt_nsec);
				ctxt->underrun_count += 1.0;
			} else {
				ctxt->overrun_nsec += (double)dt_nsec;
				ctxt->overrun_count += 1.0;
			}
		}
	} else {
		return -1;
	}
#else
	tv.tv_sec = 0;
	tv.tv_nsec = nsec;

	if (UNLIKELY(nanosleep(&tv, NULL) < 0))
		rerurn -1;
#endif
	return 0;
}

/*
 *  stress_nanosleep_pthread()
 *	pthread that performs different ranges of sleeps
 */
static void *stress_nanosleep_pthread(void *c)
{
	static void *nowt = NULL;
	stress_ctxt_t *ctxt = (stress_ctxt_t *)c;
	const stress_args_t *args = ctxt->args;

	while (stress_continue(args) &&
	       !thread_terminate &&
	       (!ctxt->max_ops || (ctxt->counter < ctxt->max_ops))) {
		register unsigned long i;
		cpu_cstate_t *cc;

		if (ctxt->mask & STRESS_NANOSLEEP_CSTATE) {
			for (cc = ctxt->cstate_list; cc; cc = cc->next) {
				if (cc->residency > 0)
					stress_nanosleep_ns(ctxt, 1000 * (long)(cc->residency + 1));
			}
		}
		if (ctxt->mask & STRESS_NANOSLEEP_RANDOM) {
			for (i = 1 << 18; i > 0; i >>=1) {
				register const unsigned long mask = (i - 1);
				long nsec = (long)(stress_mwc32() & mask) + 8;

				if (stress_nanosleep_ns(ctxt, nsec) < 0)
					break;
			}
		}
		if (ctxt->mask & STRESS_NANOSLEEP_NS)
			stress_nanosleep_ns(ctxt, 1);
		if (ctxt->mask & STRESS_NANOSLEEP_US)
			stress_nanosleep_ns(ctxt, 1000);
		if (ctxt->mask & STRESS_NANOSLEEP_MS)
			stress_nanosleep_ns(ctxt, 1000000);

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
	uint64_t max_ops;
	uint32_t i, n, limited = 0;
	uint32_t nanosleep_threads = DEFAULT_NANOSLEEP_THREADS;
	stress_ctxt_t *ctxts;
	int ret = EXIT_SUCCESS;
	int mask = STRESS_NANOSLEEP_ALL;
#if defined(HAVE_CLOCK_GETTIME) &&	\
    defined(CLOCK_MONOTONIC)
	double overhead_nsec;
	double overrun_nsec, overrun_count;
	double underrun_nsec, underrun_count;
	const uint64_t benchmark_loops = 10000;
#endif
	cpu_cstate_t *cstate_list = stress_cpuidle_cstate_list_head();

	(void)stress_get_setting("nanosleep-threads", &nanosleep_threads);
	max_ops = args->max_ops ? (args->max_ops / nanosleep_threads) + 1 : 0;

	(void)stress_get_setting("stress-nanosleep-method", &mask);
	if (mask & STRESS_NANOSLEEP_CSTATE) {
		if (cstate_list == NULL) {
			if (args->instance == 0)
				pr_inf("%s: no C states found, using just random nanosleeps instead\n", args->name);
			mask = STRESS_NANOSLEEP_RANDOM;
		} else {
			if ((args->instance == 0) &&
			    ((mask & ~STRESS_NANOSLEEP_CSTATE) == 0) &&
			    (nanosleep_threads > 1)) {
				pr_inf("%s: nanosleep-method cstate exercises C "
					"state sleeps optimally when nanosleep-threads "
					"is set to 1\n", args->name);
			}
		}
	}

	if (stress_sighandler(args->name, SIGALRM, stress_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	ctxts = calloc(nanosleep_threads, sizeof(*ctxts));
	if (!ctxts) {
		pr_inf_skip("%s: could not allocate context for %" PRIu32
			" pthreads, skipping stressor\n",
			args->name, nanosleep_threads);
		return EXIT_NO_RESOURCE;
	}

	(void)sigfillset(&set);

	for (n = 0; n < nanosleep_threads; n++) {
		ctxts[n].counter = 0;
		ctxts[n].max_ops = max_ops;
		ctxts[n].args = args;
#if defined(HAVE_CLOCK_GETTIME) && 	\
    defined(CLOCK_MONOTONIC)
		ctxts[n].overrun_nsec = 0.0;
		ctxts[n].overrun_count = 0.0;
		ctxts[n].underrun_nsec = 0.0;
		ctxts[n].underrun_count = 0.0;
#endif
		ctxts[n].mask = mask;
		ctxts[n].cstate_list = cstate_list;

		ret = pthread_create(&ctxts[n].pthread, NULL,
			stress_nanosleep_pthread, &ctxts[n]);
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
		if (!stress_continue_flag())
			goto tidy;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_bogo_set(args, 0);
		(void)shim_usleep_interruptible(10000);
		for (i = 0; i < n; i++)
			stress_bogo_add(args, ctxts[i].counter);
	}  while (!thread_terminate && stress_continue(args));

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

	overrun_count = 0.0;
	overrun_nsec = 0.0;
	underrun_count = 0.0;
	underrun_nsec = 0.0;
	for (i = 0; i < n; i++) {
		overrun_nsec += ctxts[i].overrun_nsec;
		overrun_count += (double)ctxts[i].overrun_count;
		underrun_nsec += ctxts[i].underrun_nsec;
		underrun_count += (double)ctxts[i].underrun_count;
	}

	if (underrun_count > 0.0) {
		pr_fail("%s: detected %.0f unexpected nanosleep underruns\n",
			args->name, underrun_count);
		ret = EXIT_FAILURE;
	}

	overrun_nsec -= overhead_nsec;
	overrun_nsec = (overrun_count > 0.0) ? overrun_nsec / overrun_count : 0.0;
	stress_metrics_set(args, 0, "nanosec sleep overrun", overrun_nsec);
	underrun_nsec -= overhead_nsec;
	underrun_nsec = (underrun_count > 0.0) ? underrun_nsec / underrun_count : 0.0;
	stress_metrics_set(args, 1, "nanosec sleep underrun", underrun_nsec);
#endif

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %d threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)nanosleep_threads,
			nanosleep_threads, args->instance);
	}

	free(ctxts);

	return ret;
}

stressor_info_t stress_nanosleep_info = {
	.stressor = stress_nanosleep,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_nanosleep_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pthread, librt or nanosleep() system call support"
};
#endif
