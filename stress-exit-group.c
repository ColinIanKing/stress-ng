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
#include "core-pthread.h"

#define STRESS_PTHREAD_EXIT_GROUP_MAX	(16)

static const stress_help_t help[] = {
	{ NULL,	"exit-group N",		"start N workers that exercise exit_group" },
	{ NULL,	"exit-group-ops N",	"stop exit_group workers after N bogo exit_group loops" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_PTHREAD) && 	\
    defined(__NR_exit_group)

/* per pthread data */
typedef struct {
	pthread_t pthread;	/* The pthread */
	int	  ret;		/* pthread create return */
} stress_exit_group_info_t;

static pthread_mutex_t mutex;
static volatile bool keep_running_flag;
static volatile uint64_t exit_group_failed;
static uint64_t pthread_count;
static stress_exit_group_info_t pthreads[STRESS_PTHREAD_EXIT_GROUP_MAX];

static inline void stop_running(void)
{
	keep_running_flag = false;
}

/*
 *  keep_running()
 *  	Check if SIGALRM is pending, set flags
 * 	to tell pthreads and main pthread stressor
 *	to stop. Returns false if we need to stop.
 */
static inline bool keep_running(void)
{
	if (stress_sigalrm_pending())
		stop_running();
	return keep_running_flag;
}

/*
 *  stress_exit_group_sleep()
 *	tiny delay
 */
static void stress_exit_group_sleep(void)
{
	(void)shim_nanosleep_uint64(10000);
}

/*
 *  stress_exit_group_func()
 *	pthread specific system call stressor
 */
static void *stress_exit_group_func(void *arg)
{
	(void)arg;

	stress_random_small_sleep();

	if (pthread_mutex_lock(&mutex) == 0) {
		pthread_count++;
		VOID_RET(int, pthread_mutex_unlock(&mutex));
	}

	while (keep_running_flag &&
	       (pthread_count < STRESS_PTHREAD_EXIT_GROUP_MAX)) {
		stress_exit_group_sleep();
	}
	shim_exit_group(0);

	/* should never get here */
	exit_group_failed++;
	return &g_nowt;
}

/*
 *  stress_exit_group()
 *	stress by creating pthreads
 */
static void NORETURN stress_exit_group_child(stress_args_t *args)
{
	int ret;
	sigset_t set;
	size_t i, j;

	keep_running_flag = true;

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGALRM);
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	(void)shim_memset(&pthreads, 0, sizeof(pthreads));
	ret = pthread_mutex_lock(&mutex);
	if (ret) {
		stop_running();
		shim_exit_group(0);
		exit_group_failed++;
	}
	pthread_count = 0;
	for (i = 0; i < STRESS_PTHREAD_EXIT_GROUP_MAX; i++)
		pthreads[i].ret = -1;

	for (i = 0; i < STRESS_PTHREAD_EXIT_GROUP_MAX; i++) {
		pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
			stress_exit_group_func, NULL);
		if (pthreads[i].ret) {
			/* Out of resources, don't try any more */
			if (pthreads[i].ret == EAGAIN)
				break;
			/* Something really unexpected */
			stop_running();
			(void)pthread_mutex_unlock(&mutex);
			pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
				args->name, pthreads[i].ret, strerror(pthreads[i].ret));
			shim_exit_group(0);
			exit_group_failed++;
			_exit(0);
		}
		if (UNLIKELY(!(keep_running() && stress_continue(args))))
			break;
	}
	ret = pthread_mutex_unlock(&mutex);
	if (ret) {
		stop_running();
		shim_exit_group(0);
		exit_group_failed++;
	}

	/*
	 *  Wait until they are all started or
	 *  we get bored waiting..
	 */
	for (j = 0; j < 1000; j++) {
		bool all_running = false;

		if (UNLIKELY(!stress_continue(args))) {
			stop_running();
			shim_exit_group(0);
			break;
		}

		all_running = (pthread_count == i);
		if (all_running)
			break;

		stress_exit_group_sleep();
	}
	shim_exit_group(0);
	/* Should never get here */
	exit_group_failed++;
	_exit(0);
}

/*
 *  stress_exit_group()
 *	stress by creating pthreads
 */
static int stress_exit_group(stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
        stress_set_proc_state(args->name, STRESS_STATE_RUN);

	while (stress_continue(args)) {
		pid_t pid;
		int ret;

		ret = pthread_mutex_init(&mutex, NULL);
		if (ret) {
			pr_fail("%s: pthread_mutex_init failed, errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			return EXIT_FAILURE;
		}

again:
		pid = fork();
		if (pid < 0) {
			if (stress_redo_fork(args, errno))
				goto again;
			(void)pthread_mutex_destroy(&mutex);
			break;
		} else if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_exit_group_child(args);
		} else {
			int status;
			pid_t wret;

			wret = waitpid(pid, &status, 0);
			(void)pthread_mutex_destroy(&mutex);
			if (wret < 0)
				break;

			stress_bogo_inc(args);
		}
	}

        stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (exit_group_failed > 0) {
		pr_fail("%s: at least %" PRIu64 " exit_group() calls failed to exit\n",
			args->name, exit_group_failed);
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_exit_group_info = {
	.stressor = stress_exit_group,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_exit_group_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pthread support or exit_group() system call"
};
#endif
