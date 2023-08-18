// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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

/*
 *  stress_set_loadavg_max()
 *      set upper limit on number of pthreads to create
 */
static int stress_set_loadavg_max(const char *opt)
{
	uint64_t loadavg_max;

        loadavg_max = stress_get_uint64(opt);

        stress_check_range("loadavg-max", loadavg_max, 1, MAX_LOADAVG);
        return stress_set_setting("loadavg-max", TYPE_ID_UINT64, &loadavg_max);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_loadavg_max,	stress_set_loadavg_max },
	{ 0,			NULL }
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
	static void *nowt = NULL;
	const stress_pthread_args_t *pargs = (stress_pthread_args_t *)arg;
	const stress_args_t *args = pargs->args;
#if defined(LOADAVG_IO)
	const int fd = *(int *)pargs->data;
	char buf[1];
#endif

#if defined(LOADAVG_IO)
	buf[0] = (char)stress_mwc8();
#endif

	(void)arg;
	(void)shim_nice(19);	/* be very nice */

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

	return &nowt;
}

/*
 *  stress_loadavg()
 *	stress by creating pthreads
 */
static int stress_loadavg(const stress_args_t *args)
{
	static stress_loadavg_info_t *pthreads;
	uint64_t i, j, pthread_max;
	const uint64_t threads_max = stress_loadavg_threads_max();
	const uint32_t instances = (args->num_instances > 1 ?
				   args->num_instances : 1);
	uint64_t loadavg_max = (uint64_t)instances * 65536;
	int ret;
#if defined(LOADAVG_IO)
	int fd;
	char filename[PATH_MAX];
#endif
	stress_pthread_args_t pargs = { args, NULL, 0 };
	sigset_t set;

	(void)stress_get_setting("loadavg-max", &loadavg_max);

	if (loadavg_max > threads_max) {
		loadavg_max = threads_max;
		if (args->instance == 0) {
			pr_inf("%s: not enough pthreads, reducing loadavg-max\n",
				args->name);
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

	if (args->instance == 0) {
		pr_inf("%s: attempting to create %" PRIu64 " pthreads per "
			"worker (%" PRIu64 " in total)\n",
			args->name, pthread_max, pthread_max * instances);
	}

	pthreads = calloc((size_t)pthread_max, sizeof(*pthreads));
	if (!pthreads) {
		pr_inf_skip("%s: out of memory allocating pthreads array, skipping stressor\n",
			args->name);
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
		if (!(keep_running() && stress_continue(args)))
			break;
	}

	do {
		(void)shim_sched_yield();
		shim_usleep_interruptible(100000);
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

stressor_info_t stress_loadavg_info = {
	.stressor = stress_loadavg,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_loadavg_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
