/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-pthread.h"

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
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_mutex_affinity, "mutex-affinity", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mutex_procs,    "mutex-procs",    TYPE_ID_UINT64, MIN_MUTEX_PROCS, MAX_MUTEX_PROCS, NULL },
	END_OPT,
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

#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
uint32_t *cpus;
uint32_t n_cpus;
#endif

static pthread_mutex_t ALIGN64 mutex;

typedef struct {
	stress_args_t *args;
	int prio_min;
	int prio_max;
	bool mutex_affinity;
	pthread_t pthread;
	int ret;
	double lock_duration;
	double lock_count;
} pthread_info_t;

/*
 *  stress_mutex_exercise()
 *	exercise the mutex
 */
static void OPTIMIZE3 *stress_mutex_exercise(void *arg)
{
	pthread_info_t *pthread_info = (pthread_info_t *)arg;
	stress_args_t *args = pthread_info->args;
	const int max = (pthread_info->prio_max * 7) / 8;
	int metrics_count = 0;
#if defined(HAVE_PTHREAD_MUTEXATTR)
	int mutexattr_ret;
	pthread_mutexattr_t mutexattr;
#endif

	stress_mwc_reseed();
	stress_random_small_sleep();

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
			if (UNLIKELY(pthread_mutex_lock(&mutex) != 0)) {
				pr_fail("%s: pthread_mutex_lock failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		} else {
			/* slow metrics lock path */
			double t;

			t = stress_time_now();
			if (LIKELY(pthread_mutex_lock(&mutex) == 0)) {
				pthread_info->lock_duration += stress_time_now() - t;
				pthread_info->lock_count += 1.0;
			} else {
				pr_fail("%s: pthread_mutex_lock failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
		metrics_count++;
		if (UNLIKELY(metrics_count > 1000))
			metrics_count = 0;

		param.sched_priority = pthread_info->prio_min;
		(void)pthread_setschedparam(pthread_info->pthread, SCHED_FIFO, &param);
#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
		if ((pthread_info->mutex_affinity) && (n_cpus > 0)) {
			cpu_set_t cpuset;
			const uint32_t cpu = cpus[stress_mwc32modn(n_cpus)];

			CPU_ZERO(&cpuset);
			CPU_SET(cpu, &cpuset);

			(void)pthread_setaffinity_np(pthread_info->pthread, sizeof(cpuset), &cpuset);
		}
#endif
		stress_bogo_inc(args);
		(void)shim_sched_yield();

		if (UNLIKELY(pthread_mutex_unlock(&mutex) != 0)) {
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

	return &g_nowt;
}

/*
 *  stress_mutex()
 *	stress system with priority changing mutex lock/unlocks
 */
static int stress_mutex(stress_args_t *args)
{
	size_t i;
	bool created = false;
	int prio_min, prio_max;
	pthread_info_t pthread_info[MAX_MUTEX_PROCS];
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
		pr_fail("pthread_mutex_init failed, errno=%d: "
			"(%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
	n_cpus = stress_get_usable_cpus(&cpus, true);
#endif

	prio_min = sched_get_priority_min(SCHED_FIFO);
	prio_max = sched_get_priority_max(SCHED_FIFO);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < mutex_procs; i++)
		pthread_info[i].ret = -1;

	for (i = 0; i < mutex_procs; i++) {
		pthread_info[i].args = args;
		pthread_info[i].prio_min = prio_min;
		pthread_info[i].prio_max = prio_max;
		pthread_info[i].mutex_affinity = mutex_affinity;
		pthread_info[i].lock_duration = 0.0;
		pthread_info[i].lock_count = 0.0;
		pthread_info[i].ret = pthread_create(&pthread_info[i].pthread, NULL,
                                stress_mutex_exercise, (void *)&pthread_info[i]);
		if ((pthread_info[i].ret) && (pthread_info[i].ret != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, pthread_info[i].ret, strerror(pthread_info[i].ret));
			break;
		}
		created = true;

		if (UNLIKELY(!stress_continue(args)))
			break;
	}

	if (!created) {
		pr_inf("%s: could not create any pthreads\n", args->name);
#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
		stress_free_usable_cpus(&cpus);
#endif
		return EXIT_NO_RESOURCE;
	}

	/* Wait for termination */
	while (stress_continue(args))
		(void)shim_pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < mutex_procs; i++) {
		if (pthread_info[i].ret)
			continue;

		VOID_RET(int, pthread_join(pthread_info[i].pthread, NULL));

		duration += pthread_info[i].lock_duration;
		count += pthread_info[i].lock_count;
	}
	(void)pthread_mutex_destroy(&mutex);

	rate = (count > 0.0) ? (duration / count) : 0.0;
	stress_metrics_set(args, 0, "nanosecs per mutex",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);

#if defined(HAVE_PTHREAD_SETAFFINITY_NP)
	stress_free_usable_cpus(&cpus);
#endif
	return EXIT_SUCCESS;
}

const stressor_info_t stress_mutex_info = {
	.stressor = stress_mutex,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_mutex_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, pthread_np.h, pthread or SCHED_FIFO support"
};
#endif
