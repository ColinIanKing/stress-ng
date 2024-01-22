/*
 * Copyright (C) 2024      Colin Ian King.
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
#include "core-killpid.h"
#include "core-pthread.h"

#if defined(HAVE_PTHREAD_NP_H)
#include <pthread_np.h>
#endif

#define MUTEX_PROCS	(3)

#define STRESS_PRIO_INV_TYPE_INHERIT	(0)
#define STRESS_PRIO_INV_TYPE_NONE	(1)
#define STRESS_PRIO_INV_TYPE_PROTECT	(2)

static const stress_help_t help[] = {
	{ NULL,	"prio-inv",		"start N workers exercising priority inversion lock operations" },
	{ NULL,	"prio-inv-ops N",	"stop after N priority inversion lock bogo operations" },
	{ NULL,	"prio-inv-type",	"lock protocol type, [ inherit | none | protect ]" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	const char *option;	/* prio-inv-type */
	const int  type;	/* STRESS_PRIO_INV_TYPE_* value */
} stress_prio_inv_type_t;

typedef struct {
	int prio;		/* priority level */
	pid_t pid;		/* child pid */
	double usage;		/* user + system run time usage */
} stress_prio_inv_child_info_t;

typedef struct {
	stress_prio_inv_child_info_t	child_info[MUTEX_PROCS];
	pthread_mutex_t mutex;
	stress_args_t *args;
} stress_prio_inv_info_t;

typedef void (*stress_prio_inv_func_t)(const size_t instance, stress_prio_inv_info_t *info);

static int stress_set_prio_inv_type(const char *opts)
{
	size_t i;

	static const stress_prio_inv_type_t stress_prio_inv_types[] = {
		{ "inherit",	STRESS_PRIO_INV_TYPE_INHERIT },
		{ "none",	STRESS_PRIO_INV_TYPE_NONE },
		{ "protect",	STRESS_PRIO_INV_TYPE_PROTECT },
	};

	for (i = 0; i < SIZEOF_ARRAY(stress_prio_inv_types); i++) {
		if (!strcmp(opts, stress_prio_inv_types[i].option)) {
			return stress_set_setting("prio-inv-type", TYPE_ID_INT, &stress_prio_inv_types[i].type);
		}
	}
	(void)fprintf(stderr, "prio-inv-type option '%s' not known, options are:", opts);
	for (i = 0; i < SIZEOF_ARRAY(stress_prio_inv_types); i++)
		(void)fprintf(stderr, "%s %s", i == 0 ? "" : ",", stress_prio_inv_types[i].option);
	(void)fprintf(stderr, "\n");

	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_prio_inv_type,	stress_set_prio_inv_type},
	{ 0,			NULL },
};

#if defined(_POSIX_PRIORITY_SCHEDULING) &&		\
    defined(HAVE_LIB_PTHREAD) &&			\
    defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL) &&	\
    defined(HAVE_PTHREAD_MUTEX_T) &&			\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&			\
    defined(HAVE_PTHREAD_MUTEX_DESTROY) &&		\
    defined(HAVE_SCHED_GET_PRIORITY_MIN) &&		\
    defined(HAVE_SCHED_GET_PRIORITY_MAX) &&		\
    defined(SCHED_FIFO)

static void stress_prio_inv_getrusage(stress_prio_inv_child_info_t *child_info)
{
	struct rusage usage;

	getrusage(RUSAGE_SELF, &usage);

	child_info->usage =
		(double)usage.ru_utime.tv_sec +
		((double)usage.ru_utime.tv_usec / 1000000.0) +
		(double)usage.ru_stime.tv_sec +
		((double)usage.ru_stime.tv_usec / 1000000.0);
}

static void cpu_exercise(const size_t instance, stress_prio_inv_info_t *prio_inv_info)
{
	stress_prio_inv_child_info_t *child_info = &prio_inv_info->child_info[instance];
	stress_args_t *args = prio_inv_info->args;

	do {
		stress_prio_inv_getrusage(child_info);
	} while (stress_continue(args));
}

/*
 *  mutex_exercise()
 *	exercise the mutex
 */
static void mutex_exercise(const size_t instance, stress_prio_inv_info_t *prio_inv_info)
{
	stress_prio_inv_child_info_t *child_info = &prio_inv_info->child_info[instance];
	stress_args_t *args = prio_inv_info->args;
	pthread_mutex_t *mutex = &prio_inv_info->mutex;

	do {
		if (UNLIKELY(pthread_mutex_lock(mutex) < 0)) {
			pr_fail("%s: pthread_mutex_lock failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		stress_prio_inv_getrusage(child_info);
		stress_bogo_inc(args);

		if (UNLIKELY(pthread_mutex_unlock(mutex) < 0)) {
			pr_fail("%s: pthread_mutex_unlock failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
	} while (stress_continue(args));
}

static int stress_prio_inv_set_prio(const int prio)
{
	struct sched_param param;

	(void)shim_memset(&param, 0, sizeof(param));
	param.sched_priority = prio;
	return sched_setscheduler(0, SCHED_FIFO, &param);
}

static void stress_prio_inv_alarm_handler(int sig)
{
	(void)sig;

	_exit(0);
}

/*
 *  stress_prio_inv()
 *	stress system with priority changing mutex lock/unlocks
 */
static int stress_prio_inv(stress_args_t *args)
{
	size_t i;
	int prio_min, prio_max, prio_div, prio_inv_type = STRESS_PRIO_INV_TYPE_INHERIT;
	int rc = EXIT_SUCCESS;
	const pid_t ppid = getpid();
	pthread_mutexattr_t mutexattr;
	stress_prio_inv_info_t *prio_inv_info;
	stress_prio_inv_child_info_t *child_info;
#if defined(DEBUG_USAGE)
	double total_usage;
#endif

	static const stress_prio_inv_func_t stress_prio_inv_funcs[MUTEX_PROCS] = {
		mutex_exercise,
		cpu_exercise,
		mutex_exercise,
	};

	prio_inv_info = (stress_prio_inv_info_t *)mmap(
				NULL, sizeof(*prio_inv_info),
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (prio_inv_info == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap prio_inv_info structure, errno=%d (%s), skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	child_info = prio_inv_info->child_info;
	prio_inv_info->args = args;

	(void)stress_get_setting("prio-inv-type", &prio_inv_type);

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	/*
	 *  Attempt to use priority inheritance on mutex
	 */
	if (pthread_mutexattr_init(&mutexattr) < 0) {
		pr_fail("pthread_mutexattr_init failed: errno=%d (%s)\n",
			errno, strerror(errno));
		(void)pthread_mutex_destroy(&prio_inv_info->mutex);
		return EXIT_FAILURE;
	}

	prio_min = sched_get_priority_min(SCHED_FIFO);
	prio_max = sched_get_priority_max(SCHED_FIFO);
	prio_div = (prio_max - prio_min) / (MUTEX_PROCS - 1);

	switch (prio_inv_type) {
	default:
	case STRESS_PRIO_INV_TYPE_NONE:
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_NONE));
		VOID_RET(int, pthread_mutexattr_setprioceiling(&mutexattr, prio_max));
		break;
	case STRESS_PRIO_INV_TYPE_INHERIT:
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_INHERIT));
		VOID_RET(int, pthread_mutexattr_setprioceiling(&mutexattr, prio_max));
		break;
	case STRESS_PRIO_INV_TYPE_PROTECT:
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutexattr, PTHREAD_PRIO_PROTECT));
		VOID_RET(int, pthread_mutexattr_setprioceiling(&mutexattr, prio_max));
		break;
	}
	pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST);
	if (pthread_mutex_init(&prio_inv_info->mutex, &mutexattr) < 0) {
		pr_fail("%s: pthread_mutex_init failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < MUTEX_PROCS; i++) {
		pid_t pid;

		child_info[i].prio = prio_min + (prio_div * i);
		child_info[i].usage = 0.0;

		pid = fork();
		if (pid < 0) {
			pr_inf("%s: cannot fork child process, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto reap;
		} else if (pid == 0) {
			if (stress_sighandler(args->name, SIGALRM, stress_prio_inv_alarm_handler, NULL) < 0)
				pr_inf("%s: cannot set SIGALRM signal handler, process termination may not work\n", args->name);

			child_info[i].pid = getpid();

			stress_prio_inv_set_prio(child_info[i].prio);
			stress_prio_inv_funcs[i](i, prio_inv_info);

			(void)kill(ppid, SIGALRM);
			_exit(0);
		} else {
			child_info[i].pid = pid;
		}
	}

	stress_prio_inv_set_prio(prio_max);
	/* Wait for termination */

	while (stress_continue(args))
		pause();

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

reap:
	for (i = 0; i < MUTEX_PROCS; i++) {
		if (child_info[i].pid != -1)
			(void)stress_kill_and_wait(args, child_info[i].pid, SIGALRM, false);
	}
	(void)pthread_mutexattr_destroy(&mutexattr);

#if defined(DEBUG_USAGE)
	total_usage = 0.0;
	for (i = 0; i < MUTEX_PROCS; i++) {
		total_usage += child_info[i].usage;
	}
	for (i = 0; i < MUTEX_PROCS; i++) {
		pr_inf("%zd %5.2f%% %d\n", i, child_info[i].usage / total_usage, child_info[i].prio);
	}
#endif

	switch (prio_inv_type) {
	default:
	case STRESS_PRIO_INV_TYPE_NONE:
	case STRESS_PRIO_INV_TYPE_PROTECT:
		break;
	case STRESS_PRIO_INV_TYPE_INHERIT:
		if ((child_info[2].usage < child_info[0].usage * 0.9) &&
		    (child_info[0].usage > 1.0)) {
			pr_fail("%s: mutex priority inheritance appears incorrect, low priority process has far more run time (%.2f secs) than high priority process (%.2f secs)\n",
			args->name, child_info[0].usage, child_info[2].usage);
		}
		break;
	}

	(void)pthread_mutex_destroy(&prio_inv_info->mutex);
	(void)munmap((void *)prio_inv_info, sizeof(*prio_inv_info));

	return rc;
}

stressor_info_t stress_prio_inv_info = {
	.stressor = stress_prio_inv,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_prio_inv_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, pthread_np.h, pthread or SCHED_FIFO support"
};
#endif
