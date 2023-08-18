// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_PTHREAD_NP_H)
#include <pthread_np.h>
#endif

#define MIN_MUTEX_PROCS		(2)
#define MAX_MUTEX_PROCS		(64)
#define DEFAULT_MUTEX_PROCS	(2)

static const stress_help_t help[] = {
	{ NULL,	"mutex N",		"start N workers exercising mutex operations" },
	{ NULL, "mutex-affinity",	"change CPU affinity randomly across locks" },
	{ NULL,	"mutex-ops N",		"stop after N mutex bogo operations" },
	{ NULL, "mutex-procs N",	"select the number of concurrent processes" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_mutex_affinity(const char *opt)
{
	return stress_set_setting_true("mutex-affinity", opt);
}

static int stress_set_mutex_procs(const char *opt)
{
	uint64_t mutex_procs;

	mutex_procs = stress_get_uint64(opt);
	stress_check_range("mutex-procs", mutex_procs,
		MIN_MUTEX_PROCS, MAX_MUTEX_PROCS);
	return stress_set_setting("mutex-procs", TYPE_ID_UINT64, &mutex_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mutex_affinity,	stress_set_mutex_affinity },
	{ OPT_mutex_procs,	stress_set_mutex_procs },
	{ 0,			NULL }
};

#if defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL)
#define HAVE_PTHREAD_MUTEXATTR
#endif

#if defined(_POSIX_PRIORITY_SCHEDULING) &&	\
    defined(HAVE_LIB_PTHREAD) &&		\
    defined(HAVE_PTHREAD_MUTEX_T) &&		\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEX_DESTROY) &&	\
    defined(HAVE_PTHREAD_SETSCHEDPARAM) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MIN) &&	\
    defined(HAVE_SCHED_GET_PRIORITY_MAX) &&	\
    defined(SCHED_FIFO)

static pthread_mutex_t ALIGN64 mutex;

typedef struct {
	const stress_args_t *args;
	int prio_min;
	int prio_max;
	bool mutex_affinity;
	pthread_t pthread;
	int ret;
	double lock_duration;
	double lock_count;
} pthread_info_t;

/*
 *  mutex_exercise()
 *	exercise the mutex
 */
static void OPTIMIZE3 *mutex_exercise(void *arg)
{
	pthread_info_t *pthread_info = (pthread_info_t *)arg;
	const stress_args_t *args = pthread_info->args;
	static void *nowt = NULL;
	int max = (pthread_info->prio_max * 7) / 8;
	int metrics_count = 0;
#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
	uint32_t cpus = (uint32_t)stress_get_processors_configured();
#endif
#if defined(HAVE_PTHREAD_MUTEXATTR)
	int mutexattr_ret;
	pthread_mutexattr_t mutexattr;
#endif
#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
	if (!cpus)
		cpus = 1;
#endif

	stress_mwc_reseed();

#if defined(HAVE_PTHREAD_MUTEXATTR)
	/*
	 *  Attempt to use priority inheritance on mutex
	 */
	mutexattr_ret = pthread_mutexattr_init(&mutexattr);
	if (mutexattr_ret == 0) {
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_INHERIT));
		VOID_RET(int, pthread_mutexattr_setprioceiling(&mutexattr, max));
	}
#endif

	do {
		struct sched_param param;

		param.sched_priority = max > 0 ? (int)stress_mwc32modn(max) : max;
		(void)pthread_setschedparam(pthread_info->pthread, SCHED_FIFO, &param);

		if (LIKELY(metrics_count > 0)) {
			/* fast non-metrics lock path */
			if (UNLIKELY(pthread_mutex_lock(&mutex) < 0)) {
				pr_fail("%s: pthread_mutex_lock failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		} else {
			/* slow metrics lock path */
			double t;

			t = stress_time_now();
			if (UNLIKELY(pthread_mutex_lock(&mutex) < 0)) {
				pr_fail("%s: pthread_mutex_lock failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			} else {
				pthread_info->lock_duration += stress_time_now() - t;
				pthread_info->lock_count += 1.0;
			}
		}
		metrics_count++;
		if (metrics_count > 1000)
			metrics_count = 0;

		param.sched_priority = pthread_info->prio_min;
		(void)pthread_setschedparam(pthread_info->pthread, SCHED_FIFO, &param);
#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
		if (pthread_info->mutex_affinity) {
			cpu_set_t cpuset;
			const uint32_t cpu = stress_mwc32modn(cpus);

			CPU_ZERO(&cpuset);
			CPU_SET(cpu, &cpuset);

			(void)pthread_setaffinity_np(pthread_info->pthread, sizeof(cpuset), &cpuset);
		}
#endif
		stress_bogo_inc(args);
		shim_sched_yield();

		if (UNLIKELY(pthread_mutex_unlock(&mutex) < 0)) {
			pr_fail("%s: pthread_mutex_unlock failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
	} while (stress_continue(args));

#if defined(HAVE_PTHREAD_MUTEXATTR)
	if (mutexattr_ret == 0) {
		(void)pthread_mutexattr_destroy(&mutexattr);
	}
#endif

	return &nowt;
}

/*
 *  stress_mutex()
 *	stress system with priority changing mutex lock/unlocks
 */
static int stress_mutex(const stress_args_t *args)
{
	size_t i;
	bool created = false;
	int prio_min, prio_max;
	pthread_info_t pthread_info[DEFAULT_MUTEX_PROCS];
	uint64_t mutex_procs = DEFAULT_MUTEX_PROCS;
	bool mutex_affinity = false;
	double duration = 0.0, count = 0.0, rate;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("mutex-affinity", &mutex_affinity);
	if (!stress_get_setting("mutex-procs", &mutex_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			mutex_procs = MAX_MUTEX_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			mutex_procs = MIN_MUTEX_PROCS;
	}

	(void)shim_memset(&pthread_info, 0, sizeof(pthread_info));

	if (pthread_mutex_init(&mutex, NULL) < 0) {
		pr_fail("pthread_mutex_init failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	prio_min = sched_get_priority_min(SCHED_FIFO);
	prio_max = sched_get_priority_max(SCHED_FIFO);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < DEFAULT_MUTEX_PROCS; i++) {
		pthread_info[i].args = args;
		pthread_info[i].prio_min = prio_min;
		pthread_info[i].prio_max = prio_max;
		pthread_info[i].mutex_affinity = mutex_affinity;
		pthread_info[i].lock_duration = 0.0;
		pthread_info[i].lock_count = 0.0;
		pthread_info[i].ret = pthread_create(&pthread_info[i].pthread, NULL,
                                mutex_exercise, (void *)&pthread_info[i]);
		if ((pthread_info[i].ret) && (pthread_info[i].ret != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, pthread_info[i].ret, strerror(pthread_info[i].ret));
			break;
		}
		if (!stress_continue_flag())
			break;
		created = true;
	}

	if (!created) {
		pr_inf("%s: could not create any pthreads\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Wait for termination */
	while (stress_continue(args))
		pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < DEFAULT_MUTEX_PROCS; i++) {
		if (pthread_info[i].ret)
			continue;

		duration += pthread_info[i].lock_duration;
		count += pthread_info[i].lock_count;

		VOID_RET(int, pthread_join(pthread_info[i].pthread, NULL));
	}
	(void)pthread_mutex_destroy(&mutex);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mutex", rate * STRESS_DBL_NANOSECOND);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mutex_info = {
	.stressor = stress_mutex,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_mutex_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, pthread_np.h, pthread or SCHED_FIFO support"
};
#endif
