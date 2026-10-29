/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-affinity.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-pthread.h"
#include "core-signal.h"
#include "core-workload.h"

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(64)

#define MIN_SLEEP_MS		(1)
#define MAX_SLEEP_MS		(1000)
#define DEFAULT_SLEEP_MS	(50)

#define YIELD_METHOD_ALWAYS	(0)
#define YIELD_METHOD_NEVER	(1)
#define YIELD_METHOD_RANDOM	(2)
#define DEFAULT_YIELD_METHOD	(YIELD_METHOD_RANDOM)

static const stress_help_t help[] = {
	{ NULL,	"sleepstorm N",	         "start N workers that run pthreads that sleep and wake synchronously" },
	{ NULL, "sleepstorm-cpumix",     "randomly mix threads to different CPUs" },
	{ NULL,	"sleepstorm-ops N",      "stop pthread workers after N bogo threads created" },
	{ NULL, "sleepstorm-pthreads N", "specify number of concurrent pthreads per stressor instance" },
	{ NULL, "sleepstorm-sleep-ms",   "specify sleep time in milliseconds" },
	{ NULL,	"sleepstorm-yield M",    "select yielding method [ always | never | random ]" },
	{ NULL,	NULL,                    NULL }
};

static const char * const stress_sleepstorm_methods[] = {
	"always",
	"never",
	"random",
};

static const char *stress_sleepstorm_method(const size_t i)
{
        return (i < SIZEOF_ARRAY(stress_sleepstorm_methods)) ? stress_sleepstorm_methods[i] : NULL;
}

#if defined(HAVE_LIB_PTHREAD)

/* per pthread data */
typedef struct {
	pthread_t pthread;		/* The pthread */
	int       ret;			/* pthread create return */
	double    sigwaits;
	double	  sigwait_duration;
	double	  total_duration;
} stress_sleepstorm_pthread_t;

static volatile bool keep_running_flag;
static bool sleepstorm_cpumix;
static int sleepstorm_sleep_ms = DEFAULT_SLEEP_MS;
static size_t sleepstorm_yield = DEFAULT_YIELD_METHOD;
static uint64_t sleep_us;
#if defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)
static uint32_t *sleepstorm_cpus = NULL;
static uint32_t sleepstorm_n_cpus = 0;
#endif

#endif

static const stress_opt_t opts[] = {
	{ OPT_sleepstorm_cpumix,   "sleepstorm-cpumix",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_sleepstorm_pthreads, "sleepstorm-pthreads", TYPE_ID_SIZE_T, MIN_PTHREAD, MAX_PTHREAD, NULL },
	{ OPT_sleepstorm_sleep_ms, "sleepstorm-sleep-ms", TYPE_ID_INT, MIN_SLEEP_MS, MAX_SLEEP_MS, NULL },
	{ OPT_sleepstorm_yield,    "sleepstorm-yield",    TYPE_ID_SIZE_T_METHOD, 0, 0, stress_sleepstorm_method },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD)

#define DEFAULT_STACK_MIN		(8 * KB)

static inline void stop_running(void)
{
	keep_running_flag = false;
	stress_continue_set_flag(false);
}

/*
 *  keep_running()
 *  	Check if SIGALRM is pending, set flags
 * 	to tell pthreads and main pthread stressor
 *	to stop. Returns false if we need to stop.
 */
static bool keep_running(void)
{
	if (stress_signal_alrm_pending())
		stop_running();
	return keep_running_flag;
}

/*
 *  keep_thread_running()
 *	Check if SIGALRM is pending and return false
 *	if the pthread needs to stop.
 */
static bool keep_thread_running(void)
{
	return keep_running();
}

/*
 *  stress_sleepstorm_func()
 *	pthread specific system call stressor
 */
static void * OPTIMIZE3 stress_sleepstorm_func(void *parg)
{
	const stress_pthread_args_t *spa = (stress_pthread_args_t *)parg;
	stress_sleepstorm_pthread_t *pthread = (stress_sleepstorm_pthread_t *)spa->data;
	stress_args_t *args = spa->args;
#if defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)
	pthread_t self = pthread_self();
#endif
	double t1;

	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	t1 = stress_time_now();
	while (keep_thread_running()) {
		sigset_t mask;
		int sig;
		double t2;
		double t3;
		double t4;

#if defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)
		if (sleepstorm_cpumix && (sleepstorm_cpus != NULL)) {
			const uint32_t idx = stress_mwc32modn(sleepstorm_n_cpus);
			cpu_set_t cpu_set;

			CPU_ZERO(&cpu_set);
			CPU_SET(sleepstorm_cpus[idx], &cpu_set);
			VOID_RET(int, pthread_setaffinity_np(self, sizeof(cpu_set), &cpu_set));
		}
#endif
		switch (sleepstorm_yield) {
		case YIELD_METHOD_ALWAYS:
			sched_yield();
			break;
		case YIELD_METHOD_NEVER:
			break;
		case YIELD_METHOD_RANDOM:
		default:
			if (stress_mwc1())
				sched_yield();
			break;
		}
		(void)sigemptyset(&mask);
		(void)sigaddset(&mask, SIGRTMIN);
		t2 = stress_time_now();
		(void)sigwait(&mask, &sig);
		t3 = stress_time_now();
		pthread->sigwait_duration += (t3 - t2);
		pthread->sigwaits += 1.0;
		stress_workload_waste_time(args->name, STRESS_WORKLOAD_METHOD_NOP, 0.00001, NULL, 0);

		t4 = stress_time_now();
		pthread->total_duration += t4 - t1;
		t1 = t4;
	}
	(void)keep_running();

	return &g_nowt;
}

/*
 *  stress_sleepstorm()
 *	stress by creating pthreads
 */
static int stress_sleepstorm(stress_args_t *args)
{
	sigset_t set;
	int ret;
	int rc = EXIT_SUCCESS;
	size_t sleepstorm_pthreads = DEFAULT_PTHREAD;
	size_t i;
	size_t j;
	stress_sleepstorm_pthread_t pthreads[MAX_PTHREAD];
	stress_pthread_args_t pthread_args[MAX_PTHREAD];
	double t;
	double t_next;
	double duration = 0.0;
	uint64_t running_total;
	uint64_t running_behind = 0;

	keep_running_flag = true;

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGALRM);
	(void)sigaddset(&set, SIGRTMIN);
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	(void)stress_setting_get("sleepstorm-cpumix", &sleepstorm_cpumix);
	(void)stress_setting_get("sleepstorm-pthreads", &sleepstorm_pthreads);
	(void)stress_setting_get("sleepstorm-sleep-ms", &sleepstorm_sleep_ms);
	(void)stress_setting_get("sleepstorm-yield", &sleepstorm_yield);

#if defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)
	sleepstorm_n_cpus = stress_affinity_cpus_get(&sleepstorm_cpus, true);
#else
	if (stress_instance_zero(args)) {
		pr_inf("%s: no support of setting CPU affinity, "
			"disabling option sleepstorm-cpumix\n", args->name);
        }
#endif

	(void)shim_memset(&pthreads, 0, sizeof(pthreads));
	for (i = 0; i < sleepstorm_pthreads; i++) {
		pthreads[i].ret = -1;
		pthreads[i].sigwaits = 0.0;
		pthreads[i].sigwait_duration = 0.0;
		pthreads[i].total_duration = 0.0;
	}

	for (i = 0; i < sleepstorm_pthreads; i++) {
		struct sched_param;

		if (UNLIKELY(!(keep_running() && stress_continue(args))))
			break;

		pthread_args[i].args = args;
		pthread_args[i].data = (void *)&pthreads[i];
		pthread_args[i].pthread_ret = 0;

		pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
			stress_sleepstorm_func, (void *)&pthread_args[i]);
		if (UNLIKELY(pthreads[i].ret)) {
			/* Out of resources, don't try any more */
			if (pthreads[i].ret == EAGAIN)
				break;
			pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
				args->name, pthreads[i].ret, strerror(pthreads[i].ret));
			stop_running();
			goto reap;
		}
	}

	if (i == 0) {
		pr_inf_skip("%s: unable to create any pthreads, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto reap;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	sleep_us = (uint64_t)sleepstorm_sleep_ms * 1000;
	t = stress_time_now();
	t_next = t;
	do {
		double sleep_ns;

		t_next += (double)sleep_us / 1000000.0;

		for (j = 0; j < i; j++)
			pthread_kill(pthreads[j].pthread, SIGRTMIN);

		sleep_ns = 1000000000.0 * (t_next - stress_time_now());
		if (sleep_ns > 1000000000.0)
			sleep_ns = 1000000000.0;
		if (sleep_ns > 0.0)
			shim_nanosleep_uint64((uint64_t)sleep_ns);
		else
			running_behind++;

		stress_bogo_inc(args);
	} while (keep_running() && stress_continue(args));
	duration = stress_time_now() - t;

reap:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	for (j = 0; j < i; j++) {
		if (pthreads[j].ret == 0) {
			ret = pthread_cancel(pthreads[j].pthread);
			if (UNLIKELY((ret) && (ret != ESRCH))) {
				pr_fail("%s: pthread_cancel failed (parent), errno=%d (%s)\n",
					args->name, ret, strerror(ret));
			}
			ret = pthread_join(pthreads[j].pthread, NULL);
			if (UNLIKELY((ret) && (ret != ESRCH))) {
				pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
					args->name, ret, strerror(ret));
			}
		}
	}

	if (duration > 0.0) {
		double sigwaits = 0.0;
		double sigwait_duration = 0.0;
		double total_duration = 0.0;

		for (j = 0; j < i; j++) {
			sigwaits += pthreads[j].sigwaits;
			sigwait_duration += pthreads[j].sigwait_duration;
			total_duration += pthreads[j].total_duration;
		}

		stress_metrics_set(args, "sigwaits per sec", sigwaits / duration, STRESS_METRIC_HARMONIC_MEAN);
		if (total_duration > 0.0) {
			const double percent = (100.0 * sigwait_duration / total_duration);

			stress_metrics_set(args, "% in sigwait calls", percent, STRESS_METRIC_HARMONIC_MEAN);
			stress_metrics_set(args, "% in setting affinity, yielding and overheads", 100.0 - percent, STRESS_METRIC_HARMONIC_MEAN);

		}
	}

	running_total = stress_bogo_get(args);
	if (running_total > 0) {
		const double percent = 100.0 * (double)running_behind / (double)running_total;

		stress_metrics_set(args, "% pthread_kill calls running behind time", percent, STRESS_METRIC_HARMONIC_MEAN);
	}

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("context-switches"),
	STRESS_EX_FEATURE("context-switches"),
	STRESS_EX_FEATURE("load-average"),

	STRESS_EX_SYSCALL("sigwait"),
	STRESS_EX_SYSCALL("tgkill"),

#if defined(HAVE_LIB_PTHREAD)
        STRESS_EX_LIBRARY("pthread"),
#endif

	STRESS_EX_END,
};

const stressor_info_t stress_sleepstorm_info = {
	.stressor = stress_sleepstorm,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help,
	.exercises = exercises,
};
#else
const stressor_info_t stress_sleepstorm_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
