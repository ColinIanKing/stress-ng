/*
 * Copyright (C)      2021 Canonical, Ltd.
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
#include "core-pthread.h"

#define MAX_LOADAVG	(1000000)

static const stress_help_t help[] = {
	{ NULL,	"loadavg N",	 "start N workers that create a large load average" },
	{ NULL,	"loadavg-ops N", "stop load average workers after N bogo operations" },
	{ NULL, "loadavg-max N", "set upper limit on number of pthreads to create" },
	{ NULL,	NULL,		 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_loadavg_max, "loadavg-max", TYPE_ID_UINT64, 1, MAX_LOADAVG, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD)

/*
 *  Linux includes blocked I/O tasks in load average, so
 *  enable I/O activity to try and boost load average higher
 */
#if defined(__linux__)
#define LOADAVG_IO		(1)
#endif

/* per pthread data */
typedef struct {
	pthread_t pthread;	/* The pthread */
	int	  ret;		/* pthread create return */
} stress_loadavg_info_t;

static volatile bool keep_thread_running_flag;
static volatile bool keep_running_flag;

static inline void stop_running(void)
{
	keep_running_flag = false;
	keep_thread_running_flag = false;
}

/*
 *  keep_running()
 *  	Check if SIGALRM is pending, set flags
 * 	to tell pthreads and main pthread stressor
 *	to stop. Returns false if we need to stop.
 */
static bool keep_running(void)
{
	if (stress_sigalrm_pending())
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
	return keep_running() && keep_thread_running_flag;
}

/*
 *  stress_loadavg_threads_max()
 *	determine maximum number of threads allowed in system,
 *	return 0 if unknown
 */
static uint64_t stress_loadavg_threads_max(void)
{
#if defined(__linux__)
	char buf[64];
	ssize_t ret;
	uint64_t max;

	ret = stress_system_read("/proc/sys/kernel/threads-max", buf, sizeof(buf));
	if (ret < 0)
		return 0;

	if (sscanf(buf, "%" SCNu64, &max) != 1)
		return 0;

	return max;
#else
	return 0;
#endif
}

/*
 *  stress_loadavg_func()
 *	pthread specific system call stressor
 */
static void *stress_loadavg_func(void *arg)
{
	const stress_pthread_args_t *pargs = (stress_pthread_args_t *)arg;
	stress_args_t *args = pargs->args;
#if defined(LOADAVG_IO)
	const int fd = *(int *)pargs->data;
	char buf[1];
#endif

#if defined(LOADAVG_IO)
	buf[0] = (char)stress_mwc8();
#endif

	(void)arg;
	(void)shim_nice(19);	/* be very nice */

	stress_random_small_sleep();

	while ((stress_time_now() < args->time_end) && keep_thread_running()) {
#if defined(LOADAVG_IO)
		if (fd >= 0) {
			VOID_RET(off_t, lseek(fd, (off_t)stress_mwc16(), SEEK_SET));
			VOID_RET(ssize_t, write(fd, buf, sizeof(buf)));
		}
#endif
		stress_bogo_inc(pargs->args);
		(void)shim_sched_yield();
	}

	(void)keep_running();

	return &g_nowt;
}

/*
 *  stress_loadavg()
 *	stress by creating pthreads
 */
static int stress_loadavg(stress_args_t *args)
{
	static stress_loadavg_info_t *pthreads;
	uint64_t i, j, pthread_max;
	const uint64_t threads_max = stress_loadavg_threads_max();
	const uint32_t instances = (args->instances > 1 ?
				   args->instances : 1);
	uint64_t loadavg_max = (uint64_t)instances * 65536;
	int ret;
#if defined(LOADAVG_IO)
	int fd;
	char filename[PATH_MAX];
#endif
	stress_pthread_args_t pargs = { args, NULL, 0 };
	sigset_t set;

	if (g_opt_flags & OPT_FLAGS_MINIMIZE)
		loadavg_max = 4;	/* Really low */

	(void)stress_get_setting("loadavg-max", &loadavg_max);

	if (loadavg_max > threads_max) {
		loadavg_max = threads_max;
		if (stress_instance_zero(args)) {
			pr_inf("%s: not enough pthreads, reducing loadavg-max, system limit is %" PRIu64 "\n",
				args->name, threads_max);
		}
	}

	keep_running_flag = true;
	if (threads_max > 0)
		pthread_max = loadavg_max / instances;
	else
		pthread_max = loadavg_max;

	if (pthread_max < 1)
		pthread_max = 1;
	if (pthread_max * instances > loadavg_max)
		pthread_max = loadavg_max / instances;

	if (stress_instance_zero(args)) {
		pr_inf("%s: attempting to create %" PRIu64 " pthreads per "
			"worker (%" PRIu64 " in total)\n",
			args->name, pthread_max, pthread_max * instances);
	}

	pthreads = (stress_loadavg_info_t *)calloc((size_t)pthread_max, sizeof(*pthreads));
	if (!pthreads) {
		pr_inf_skip("%s: out of memory allocating pthreads array%s, skipping stressor\n",
			args->name, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

#if defined(LOADAVG_IO)
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0) {
		free(pthreads);
		return stress_exit_status((int)-ret);
	}

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	/*  Not a failure if can't open a file */
	fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, S_IRUSR | S_IWUSR);
	(void)shim_unlink(filename);
#endif

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGALRM);
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	keep_thread_running_flag = true;

	for (i = 0; i < pthread_max; i++)
		pthreads[i].ret = -1;

	for (i = 0; i < pthread_max; i++) {
#if defined(LOADAVG_IO)
		pargs.data = &fd;
#else
		pargs.data = NULL;
#endif

		pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
			stress_loadavg_func, (void *)&pargs);
		if (pthreads[i].ret) {
			/* Out of resources, don't try any more */
			if (pthreads[i].ret == EAGAIN)
				break;
			/* Something really unexpected */
			pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
				args->name, pthreads[i].ret, strerror(pthreads[i].ret));
			stop_running();
			break;
		}
		if (UNLIKELY(!(keep_running() && stress_continue(args))))
			break;
	}

	do {
		(void)shim_sched_yield();
		(void)shim_usleep_interruptible(100000);
	} while (keep_running() && stress_continue(args));

	keep_thread_running_flag = false;

	for (j = 0; j < i; j++) {
		if (pthreads[j].ret == 0) {
			ret = pthread_join(pthreads[j].pthread, NULL);
			if ((ret) && (ret != ESRCH)) {
				pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
			}
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(LOADAVG_IO)
	if (fd >= 0)
		(void)close(fd);
	(void)stress_temp_dir_rm_args(args);
#endif
	free(pthreads);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_loadavg_info = {
	.stressor = stress_loadavg,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_loadavg_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
