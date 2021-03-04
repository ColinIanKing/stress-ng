/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"sem N",	"start N workers doing semaphore operations" },
	{ NULL,	"sem-ops N",	"stop after N semaphore bogo operations" },
	{ NULL,	"sem-procs N",	"number of processes to start per worker" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_semaphore_posix_procs(const char *opt)
{
	uint64_t semaphore_posix_procs;

	semaphore_posix_procs = stress_get_uint64(opt);
	stress_check_range("sem-procs", semaphore_posix_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
	return stress_set_setting("sem-procs", TYPE_ID_UINT64, &semaphore_posix_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sem_procs,	stress_set_semaphore_posix_procs },
	{ 0,			NULL }
};

#if defined(HAVE_SEMAPHORE_H) && \
    defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_SEM_POSIX)

static sem_t sem;
static pthread_t pthreads[MAX_SEMAPHORE_PROCS];
static int p_ret[MAX_SEMAPHORE_PROCS];

/*
 *  semaphore_posix_thrash()
 *	exercise the semaphore
 */
static void *semaphore_posix_thrash(void *arg)
{
	const stress_pthread_args_t *p_args = arg;
	const stress_args_t *args = p_args->args;
	static void *nowt = NULL;

	do {
		int i;

		for (i = 0; keep_stressing_flag() && i < 1000; i++) {
			int value;

			if (sem_getvalue(&sem, &value) < 0)
				pr_fail("%s: sem_getvalue failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));

			if (i & 1) {
				if (sem_trywait(&sem) < 0) {
					if (errno == 0 ||
					    errno == EAGAIN)
						continue;
					if (errno != EINTR)
						pr_fail("%s: sem_trywait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					break;
				}
			} else {
				struct timespec ts;

#if defined(CLOCK_REALTIME)
				if (clock_gettime(CLOCK_REALTIME, &ts) < 0)
					(void)memset(&ts, 0, sizeof(ts));
#else
				(void)memset(&ts, 0, sizeof(ts));
#endif

				if (sem_timedwait(&sem, &ts) < 0) {
					if (errno == 0 ||
					    errno == EAGAIN ||
					    errno == ETIMEDOUT)
						continue;
					if (errno != EINTR)
						pr_fail("%s: sem_timedwait failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					break;
				}
			}
			inc_counter(args);
			if (sem_post(&sem) < 0) {
				pr_fail("%s: sem_post failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (stress_mwc1())
				(void)shim_sched_yield();
			else
				(void)shim_usleep(0);
		}
	} while (keep_stressing(args));

	return &nowt;
}

/*
 *  stress_sem()
 *	stress system by POSIX sem ops
 */
static int stress_sem(const stress_args_t *args)
{
	uint64_t semaphore_posix_procs = DEFAULT_SEMAPHORE_PROCS;
	uint64_t i;
	bool created = false;
	stress_pthread_args_t p_args;

	if (!stress_get_setting("sem-procs", &semaphore_posix_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			semaphore_posix_procs = MAX_SEMAPHORE_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			semaphore_posix_procs = MIN_SEMAPHORE_PROCS;
	}

	/* create a semaphore */
	if (sem_init(&sem, 0, 1) < 0) {
		pr_err("semaphore init (POSIX) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)memset(pthreads, 0, sizeof(pthreads));
	(void)memset(p_ret, 0, sizeof(p_ret));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < semaphore_posix_procs; i++) {
		p_args.args = args;
		p_args.data = NULL;
		p_ret[i] = pthread_create(&pthreads[i], NULL,
                                semaphore_posix_thrash, (void *)&p_args);
		if ((p_ret[i]) && (p_ret[i] != EAGAIN)) {
			pr_fail("%s: pthread create failed, errno=%d (%s)\n",
				args->name, p_ret[i], strerror(p_ret[i]));
			break;
		}
		if (!keep_stressing_flag())
			break;
		created = true;
	}

	if (!created) {
		pr_inf("%s: could not create any pthreads\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/* Wait for termination */
	while (keep_stressing(args))
		pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < semaphore_posix_procs; i++) {
		int ret;

		if (p_ret[i])
			continue;

		ret = pthread_join(pthreads[i], NULL);
		(void)ret;
	}
	(void)sem_destroy(&sem);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sem_info = {
	.stressor = stress_sem,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sem_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
