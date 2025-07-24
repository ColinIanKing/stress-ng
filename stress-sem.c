/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-mmap.h"
#include "core-pthread.h"

#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#else
UNEXPECTED
#endif

#define MIN_SEM_POSIX_PROCS     (2)
#define MAX_SEM_POSIX_PROCS     (64)
#define DEFAULT_SEM_POSIX_PROCS (2)

static const stress_help_t help[] = {
	{ NULL,	"sem N",	"start N workers doing semaphore operations" },
	{ NULL,	"sem-ops N",	"stop after N semaphore bogo operations" },
	{ NULL,	"sem-procs N",	"number of processes to start per worker" },
	{ NULL,	"sem-shared",	"share the semaphore across all semaphore stressors" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_sem_procs,  "sem-procs",  TYPE_ID_UINT64, MIN_SEM_POSIX_PROCS, MAX_SEM_POSIX_PROCS, NULL },
	{ OPT_sem_shared, "sem-shared", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SEMAPHORE_H) && \
    defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_SEM_POSIX)

typedef struct {
	pthread_t pthread;
	int ret;
	double trywait_count;
	double timedwait_count;
	double wait_count;
} stress_sem_pthread_t;

static sem_t sem_local;
static sem_t *sem_global;
static sem_t *sem;
static int sem_global_errno;

static stress_sem_pthread_t sem_pthreads[MAX_SEM_POSIX_PROCS] ALIGN64;

static void stress_sem_init(const uint32_t instances)
{
	(void)instances;

	sem_global_errno = 0;
	sem_global = (sem_t *)stress_mmap_populate(NULL, sizeof(*sem_global),
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sem_global == MAP_FAILED) {
		sem_global_errno = errno;
		sem_global = NULL;
		return;
	}
	stress_set_vma_anon_name(sem_global, sizeof(*sem_global), "shared-semaphore");
	if (sem_init(sem_global, 0, (unsigned int)instances) < 0) {
		sem_global_errno = errno;
		(void)munmap((void *)sem_global, sizeof(*sem_global));
		sem_global = NULL;
	}
}

static void stress_sem_deinit(void)
{
	if (sem_global) {
		(void)sem_destroy(sem_global);
		(void)munmap((void *)sem_global, sizeof(*sem_global));
		sem_global = NULL;
	}
}

/*
 *  stress_sem_thrash()
 *	exercise the semaphore
 */
static void OPTIMIZE3 *stress_sem_thrash(void *arg)
{
	const stress_pthread_args_t *p_args = arg;
	stress_args_t *args = p_args->args;
	stress_sem_pthread_t *pthread = (stress_sem_pthread_t *)p_args->data;

	stress_random_small_sleep();

	do {
		int i, j = -1;

		for (i = 0; LIKELY((i < 1000) && stress_continue_flag()); i++) {
			int value;
			struct timespec ts;

			if (UNLIKELY(sem_getvalue(sem, &value) < 0)) {
				pr_fail("%s: sem_getvalue failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
do_semwait:
			j++;
			if (UNLIKELY(j >= 3))
				j = 0;

			switch (j) {
			case 0:
				/* Attempt a try wait */
				if (UNLIKELY(sem_trywait(sem) < 0)) {
					if (LIKELY(errno == EAGAIN))
						goto do_semwait;
					if (UNLIKELY(errno != EINTR))
						pr_fail("%s: sem_trywait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					goto do_return;
				}
				pthread->trywait_count += 1.0;
				break;
			case 1:
				/* Attempt a very short timed wait */
#if defined(CLOCK_REALTIME)
				if (UNLIKELY(clock_gettime(CLOCK_REALTIME, &ts) < 0))
					(void)shim_memset(&ts, 0, sizeof(ts));
#else
				(void)shim_memset(&ts, 0, sizeof(ts));
#endif
				ts.tv_nsec += 10000;
				if (ts.tv_nsec >= 1000000000) {
					ts.tv_nsec -= 1000000000;
					ts.tv_sec++;
				}

				if (UNLIKELY(sem_timedwait(sem, &ts) < 0)) {
					if (LIKELY(errno == EAGAIN ||
						   errno == ETIMEDOUT ||
						   errno == EINVAL))
						goto do_semwait;
					if (UNLIKELY(errno != EINTR))
						pr_fail("%s: sem_timedwait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					goto do_return;
				}
				pthread->timedwait_count += 1.0;
				break;
			case 2:
				if (UNLIKELY(sem_wait(sem) < 0)) {
					if (UNLIKELY(errno != EINTR))
						pr_fail("%s: sem_wait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					goto do_return;
				}
				pthread->wait_count += 1.0;
				break;
			default:
				pr_inf("%s: bug detected, invalid state %d\n", args->name, j);
				goto do_return;
			}

			/* Locked at this point, bump counter */
			stress_bogo_inc(args);

			if (UNLIKELY(sem_post(sem) < 0)) {
				pr_fail("%s: sem_post failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto do_return;
			}

			/*
			 *  force a scheduling yield to try and force alternative
			 *  thread(s) to have a turn on wait/post combo
			 */
			(void)shim_sched_yield();
		}
	} while (stress_continue(args));

do_return:
	return &g_nowt;
}

/*
 *  stress_sem()
 *	stress system by POSIX sem ops
 */
static int stress_sem(stress_args_t *args)
{
	uint64_t semaphore_posix_procs = DEFAULT_SEM_POSIX_PROCS;
	uint64_t i;
	bool created = false;
	bool sem_shared = false;
	stress_pthread_args_t p_args;
	double wait_count = 0;
	double trywait_count = 0;
	double timedwait_count = 0;
	double t, duration;

	if (!stress_get_setting("sem-procs", &semaphore_posix_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			semaphore_posix_procs = MAX_SEM_POSIX_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			semaphore_posix_procs = MIN_SEM_POSIX_PROCS;
	}
	if (!stress_get_setting("sem-shared", &sem_shared)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			sem_shared = true;
	}

	if (sem_shared) {
		if (!sem_global) {
			pr_fail("%s: semaphore init (POSIX) failed, failed to "
				"mmap or init shared semaphore, errno=%d (%s)\n",
				args->name, sem_global_errno, strerror(sem_global_errno));
			return EXIT_FAILURE;
		}
		sem = sem_global;
	} else {
		/* create a local semaphore */
		if (sem_init(&sem_local, 0, 1) < 0) {
			pr_fail("%s: semaphore init (POSIX) failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		sem = &sem_local;
	}

	(void)shim_memset(sem_pthreads, 0, sizeof(sem_pthreads));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	for (i = 0; i < semaphore_posix_procs; i++) {
		sem_pthreads[i].trywait_count = 0.0;
		sem_pthreads[i].timedwait_count = 0.0;
		sem_pthreads[i].wait_count = 0.0;

		p_args.args = args;
		p_args.data = &sem_pthreads[i];
		sem_pthreads[i].ret = pthread_create(&sem_pthreads[i].pthread, NULL,
                                stress_sem_thrash, (void *)&p_args);
		if ((sem_pthreads[i].ret) && (sem_pthreads[i].ret != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, sem_pthreads[i].ret, strerror(sem_pthreads[i].ret));
			break;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
		created = true;
	}

	if (!created) {
		pr_inf("%s: could not create any pthreads\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Wait for termination */
	while (stress_continue(args))
		(void)shim_pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < semaphore_posix_procs; i++) {
		if (sem_pthreads[i].ret)
			continue;

		VOID_RET(int, pthread_cancel(sem_pthreads[i].pthread));

		trywait_count += sem_pthreads[i].trywait_count;
		timedwait_count += sem_pthreads[i].timedwait_count;
		wait_count += sem_pthreads[i].wait_count;
	}
	duration = stress_time_now() - t;
	if (duration > 0.0) {
		stress_metrics_set(args, 0, "sem_trywait calls per sec",
			trywait_count / duration, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 1, "sem_timedwait calls per sec",
			timedwait_count / duration, STRESS_METRIC_HARMONIC_MEAN);
		stress_metrics_set(args, 2, "sem_wait calls per sec",
			wait_count / duration, STRESS_METRIC_HARMONIC_MEAN);
	}

	if (!sem_shared)
		(void)sem_destroy(&sem_local);

	/* ready the bogo flag duo to cancelling a pthread while updating a bogo-op */
	stress_bogo_ready(args);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_sem_info = {
	.stressor = stress_sem,
	.classifier = CLASS_OS | CLASS_SCHEDULER | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.init = stress_sem_init,
	.deinit = stress_sem_deinit,
	.help = help
};
#else
const stressor_info_t stress_sem_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_SCHEDULER | CLASS_IPC,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without semaphore.h, pthread or POSIX semaphore support"
};
#endif
