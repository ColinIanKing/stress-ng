/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_sem_procs, "sem-procs", TYPE_ID_UINT64, MIN_SEM_POSIX_PROCS, MAX_SEM_POSIX_PROCS, NULL },
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

static sem_t sem;

static stress_sem_pthread_t sem_pthreads[MAX_SEM_POSIX_PROCS] ALIGN64;

/*
 *  semaphore_posix_thrash()
 *	exercise the semaphore
 */
static void OPTIMIZE3 *semaphore_posix_thrash(void *arg)
{
	const stress_pthread_args_t *p_args = arg;
	stress_args_t *args = p_args->args;
	stress_sem_pthread_t *pthread = (stress_sem_pthread_t *)p_args->data;
	static void *nowt = NULL;

	do {
		int i, j = -1;

		for (i = 0; (i < 1000) && stress_continue_flag(); i++) {
			int value;
			struct timespec ts;

			if (UNLIKELY(sem_getvalue(&sem, &value) < 0)) {
				pr_fail("%s: sem_getvalue failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
do_semwait:
			j++;
			if (j >= 3)
				j = 0;

			switch (j) {
			case 0:
				/* Attempt a try wait */
				if (sem_trywait(&sem) < 0) {
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

				if (sem_timedwait(&sem, &ts) < 0) {
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
				if (UNLIKELY(sem_wait(&sem) < 0)) {
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

			if (UNLIKELY(sem_post(&sem) < 0)) {
				pr_fail("%s: sem_post failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto do_return;
			}

			/*
			 *  force a scheduling yield to try and force alternative
			 *  thread(s) to have a turn on wait/post combo
			 */
			shim_sched_yield();
		}
	} while (stress_continue(args));

do_return:
	return &nowt;
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

	/* create a semaphore */
	if (sem_init(&sem, 0, 1) < 0) {
		pr_fail("semaphore init (POSIX) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)shim_memset(sem_pthreads, 0, sizeof(sem_pthreads));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	t = stress_time_now();
	for (i = 0; i < semaphore_posix_procs; i++) {
		sem_pthreads[i].trywait_count = 0.0;
		sem_pthreads[i].timedwait_count = 0.0;
		sem_pthreads[i].wait_count = 0.0;

		p_args.args = args;
		p_args.data = &sem_pthreads[i];
		sem_pthreads[i].ret = pthread_create(&sem_pthreads[i].pthread, NULL,
                                semaphore_posix_thrash, (void *)&p_args);
		if ((sem_pthreads[i].ret) && (sem_pthreads[i].ret != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, sem_pthreads[i].ret, strerror(sem_pthreads[i].ret));
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

	for (i = 0; i < semaphore_posix_procs; i++) {
		if (sem_pthreads[i].ret)
			continue;

		VOID_RET(int, pthread_join(sem_pthreads[i].pthread, NULL));

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

	(void)sem_destroy(&sem);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sem_info = {
	.stressor = stress_sem,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sem_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without semaphore.h, pthread or POSIX semaphore support"
};
#endif
