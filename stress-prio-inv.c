/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-pthread.h"

#if defined(HAVE_PTHREAD_NP_H)
#include <pthread_np.h>
#endif

/*
#define DEBUG_USAGE
*/

#define MUTEX_PROCS	(3)

#if defined(HAVE_PTHREAD_PRIO_INHERIT)
#define STRESS_PRIO_INV_TYPE_INHERIT	(PTHREAD_PRIO_INHERIT)
#else
#define STRESS_PRIO_INV_TYPE_INHERIT	(-1)
#endif

#if defined(HAVE_PTHREAD_PRIO_NONE)
#define STRESS_PRIO_INV_TYPE_NONE	(PTHREAD_PRIO_NONE)
#else
#define STRESS_PRIO_INV_TYPE_NONE	(-2)
#endif

#if defined(HAVE_PTHREAD_PRIO_PROTECT)
#define STRESS_PRIO_INV_TYPE_PROTECT	(PTHREAD_PRIO_PROTECT)
#else
#define STRESS_PRIO_INV_TYPE_PROTECT	(-3)
#endif

/* must match order in stress_prio_inv_policies[] */
#if defined(SCHED_BATCH)
#define STRESS_PRIO_INV_POLICY_BATCH	(SCHED_BATCH)
#else
#define STRESS_PRIO_INV_POLICY_BATCH	(-1)
#endif

#if defined(SCHED_IDLE)
#define STRESS_PRIO_INV_POLICY_IDLE	(SCHED_IDLE)
#else
#define STRESS_PRIO_INV_POLICY_IDLE	(-2)
#endif

#if defined(SCHED_FIFO)
#define STRESS_PRIO_INV_POLICY_FIFO	(SCHED_FIFO)
#else
#define STRESS_PRIO_INV_POLICY_FIFO	(-3)
#endif

#if defined(SCHED_OTHER)
#define STRESS_PRIO_INV_POLICY_OTHER	(SCHED_OTHER)
#else
#define STRESS_PRIO_INV_POLICY_OTHER	(-5)
#endif

#if defined(SCHED_RR)
#define STRESS_PRIO_INV_POLICY_RR	(SCHED_RR)
#else
#define STRESS_PRIO_INV_POLICY_RR	(-6)
#endif

#if defined(SCHED_EXT)
#define STRESS_PRIO_INV_POLICY_EXT	(SCHED_EXT)
#else
#define STRESS_PRIO_INV_POLICY_EXT	(-7)
#endif

static const stress_help_t help[] = {
	{ NULL,	"prio-inv",		"start N workers exercising priority inversion lock operations" },
	{ NULL,	"prio-inv-ops N",	"stop after N priority inversion lock bogo operations" },
	{ NULL, "prio-inv-policy P",	"select scheduler policy [ batch | ext | idle | fifo | other | rr ]" },
	{ NULL,	"prio-inv-type T",	"pthread priority type [ inherit | none | protect ]" },
	{ NULL,	NULL,			NULL }
};

typedef struct {
	const char *option;	/* prio-inv-type */
	const int  value;	/* STRESS_PRIO_INV_* value */
} stress_prio_inv_options_t;

typedef struct {
	int prio;		/* priority level */
	int niceness;		/* niceness */
	pid_t pid;		/* child pid */
	double usage;		/* user + system run time usage */
} stress_prio_inv_child_info_t;

static const stress_prio_inv_options_t stress_prio_inv_types[] = {
	{ "inherit",	STRESS_PRIO_INV_TYPE_INHERIT },
	{ "none",	STRESS_PRIO_INV_TYPE_NONE },
	{ "protect",	STRESS_PRIO_INV_TYPE_PROTECT },
};

static const stress_prio_inv_options_t stress_prio_inv_policies[] = {
	{ "batch",	STRESS_PRIO_INV_POLICY_BATCH },
	{ "ext",	STRESS_PRIO_INV_POLICY_EXT },
	{ "idle",	STRESS_PRIO_INV_POLICY_IDLE },
	{ "fifo",	STRESS_PRIO_INV_POLICY_FIFO },
	{ "other",	STRESS_PRIO_INV_POLICY_OTHER },
	{ "rr",		STRESS_PRIO_INV_POLICY_RR },
};

static const char *stress_prio_inv_policy(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_prio_inv_policies)) ? stress_prio_inv_policies[i].option : NULL;
}

static const char *stress_prio_inv_type(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_prio_inv_types)) ? stress_prio_inv_types[i].option : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_prio_inv_policy, "prio-inv-policy", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_prio_inv_policy },
	{ OPT_prio_inv_type,   "prio-inv-type",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_prio_inv_type },
	END_OPT,
};

#if defined(_POSIX_PRIORITY_SCHEDULING) &&		\
    defined(HAVE_LIB_PTHREAD) &&			\
    defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETROBUST) &&	\
    defined(HAVE_PTHREAD_MUTEX_T) &&			\
    defined(HAVE_PTHREAD_MUTEX_INIT) &&			\
    defined(HAVE_PTHREAD_MUTEX_DESTROY) &&		\
    defined(HAVE_SETPRIORITY) &&			\
    defined(HAVE_SCHED_SETSCHEDULER) &&			\
    defined(HAVE_SCHED_GET_PRIORITY_MIN) &&		\
    defined(HAVE_SCHED_GET_PRIORITY_MAX) &&		\
    (defined(SCHED_BATCH) ||				\
     defined(SCHED_EXT) ||				\
     defined(SCHED_FIFO) ||				\
     defined(SCHED_IDLE) |				\
     defined(SCHED_OTHER) ||				\
     defined(SCHED_RR))

static double t_end = 0.0;

typedef struct {
	stress_prio_inv_child_info_t	child_info[MUTEX_PROCS];
	pthread_mutex_t mutex;
	stress_args_t *args;
} stress_prio_inv_info_t;

typedef void (*stress_prio_inv_func_t)(const size_t instance, stress_prio_inv_info_t *info);

static void stress_prio_inv_getrusage(stress_prio_inv_child_info_t *child_info)
{
	struct rusage usage;

	getrusage(RUSAGE_SELF, &usage);

	child_info->usage =
		(double)usage.ru_utime.tv_sec +
		((double)usage.ru_utime.tv_usec / 1000000.0);
	/*
	 * https://github.com/ColinIanKing/stress-ng/issues/534
	 * - don't include system time usage as this can be
	 *   overly high overhead on slower systems. We really
	 *   are caring about the comparison of user times
	 *
	 *      + (double)usage.ru_stime.tv_sec
	 *	+ ((double)usage.ru_stime.tv_usec / 1000000.0);
	 */
}

static void cpu_exercise(const size_t instance, stress_prio_inv_info_t *prio_inv_info)
{
	stress_prio_inv_child_info_t *child_info = &prio_inv_info->child_info[instance];
	stress_args_t *args = prio_inv_info->args;

	do {
		stress_prio_inv_getrusage(child_info);
	} while (stress_continue(args) && (stress_time_now() < t_end));
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
	} while (stress_continue(args) && (stress_time_now() < t_end));
}

static int stress_prio_inv_set_prio_policy(
	stress_args_t *args,
	const int prio,
	const int niceness,
	int policy)
{
	struct sched_param param;
	int ret;

redo_policy:
	switch (policy) {
#if defined(SCHED_FIFO)
	case SCHED_FIFO:
#endif
#if defined(SCHED_RR)
	case SCHED_RR:
#endif
#if defined(SCHED_FIFO) || defined(SCHED_RR)
		(void)shim_memset(&param, 0, sizeof(param));
		param.sched_priority = prio;
		ret = sched_setscheduler(0, policy, &param);
		if (ret < 0) {
			if (errno == EPERM) {
				static bool warned = false;

				if (!warned) {
					warned = true;
					pr_inf("%s: cannot set scheduling priority to %d and policy %s, "
						"no permission, retrying with 'other'\n",
						args->name, prio, stress_get_sched_name(policy));
				}
				param.sched_priority = 0;
				policy = SCHED_OTHER;
				goto redo_policy;
			}
			pr_fail("%s: cannot set scheduling priority to %d and policy %s, errno=%d (%s)\n",
				args->name, prio, stress_get_sched_name(policy),
				errno, strerror(errno));
		}
		break;
#endif
	default:
		(void)shim_memset(&param, 0, sizeof(param));
		param.sched_priority = 0;
		ret = sched_setscheduler(0, policy, &param);
		if (ret < 0) {
			pr_fail("%s: cannot set scheduling priority to %d and policy %s, errno=%d (%s)\n",
				args->name, prio, stress_get_sched_name(policy),
				errno, strerror(errno));
		}
		ret = setpriority(PRIO_PROCESS, 0, niceness);
		if (ret < 0) {
			pr_fail("%s: cannot set priority to %d, errno=%d (%s)\n",
				args->name, niceness, errno, strerror(errno));
		}
	}
	return ret;
}

static void stress_prio_inv_alarm_handler(int sig)
{
	(void)sig;

	_exit(0);
}

#if defined(SCHED_FIFO) ||	\
    defined(SCHED_RR)
static void stress_prio_inv_check_policy(
	stress_args_t *args,
	const int policy,
	int *sched_policy,
	const char *policy_name)
{
	if (!stress_check_capability(SHIM_CAP_IS_ROOT)) {
		if (*sched_policy == policy) {
			if (stress_instance_zero(args)) {
				pr_inf("%s: cannot set prio-inv policy '%s' as non-root user, "
					"defaulting to 'other'\n",
					args->name, policy_name);
			}
#if defined(SCHED_OTHER)
			*sched_policy = SCHED_OTHER;
#else
			*sched_policy = -1;	/* Unknown! */
#endif
		}
	}
}
#endif

/*
 *  stress_prio_inv()
 *	stress system with priority changing mutex lock/unlocks
 */
static int stress_prio_inv(stress_args_t *args)
{
	size_t i;
	int prio_min, prio_max, prio_div, sched_policy = -1;
	size_t prio_inv_type = 0; /* STRESS_PRIO_INV_TYPE_INHERIT */
	size_t prio_inv_policy = 2; /* STRESS_PRIO_INV_POLICY_FIFO */
	int pthread_protocol;
	int nice_min, nice_max, nice_div;
	int rc = EXIT_SUCCESS;
	const pid_t ppid = getpid();
	pthread_mutexattr_t mutexattr;
	stress_prio_inv_info_t *prio_inv_info;
	stress_prio_inv_child_info_t *child_info;
	const char *policy_name;
#if defined(DEBUG_USAGE)
	double total_usage;
#endif

	static const stress_prio_inv_func_t stress_prio_inv_funcs[MUTEX_PROCS] = {
		mutex_exercise,
		cpu_exercise,
		mutex_exercise,
	};

	t_end = stress_time_now() + (double)g_opt_timeout;

	prio_inv_info = (stress_prio_inv_info_t *)mmap(
				NULL, sizeof(*prio_inv_info),
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (prio_inv_info == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu byte prio_inv_info structure%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, sizeof(*prio_inv_info),
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(prio_inv_info, sizeof(*prio_inv_info), "state");
	child_info = prio_inv_info->child_info;
	prio_inv_info->args = args;

	(void)stress_get_setting("prio-inv-type", &prio_inv_type);
	(void)stress_get_setting("prio-inv-policy", &prio_inv_policy);

	policy_name = stress_prio_inv_policies[prio_inv_policy].option;
	sched_policy = stress_prio_inv_policies[prio_inv_policy].value;

	if (sched_policy < 0) {
#if defined(SCHED_OTHER)
		if (stress_instance_zero(args)) {
			pr_inf("%s: scheduling policy '%s' is not supported, "
				"defaulting to 'other'\n",
				args->name, policy_name);
			sched_policy = SCHED_OTHER;
		}
#else
		if (stress_instance_zero(args)) {
			pr_inf_skip("%s: scheduling policy '%s' is not supported, "
				"no default 'other' either, skipping stressor\n",
				args->name, policy_name);
		}
		rc = EXIT_NO_RESOURCE;
		goto unmap_prio_inv_info;
#endif
	}

#if defined(SCHED_FIFO)
	stress_prio_inv_check_policy(args, SCHED_FIFO, &sched_policy, policy_name);
#endif
#if defined(SCHED_RR)
	stress_prio_inv_check_policy(args, SCHED_RR, &sched_policy, policy_name);
#endif

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	/*
	 *  Attempt to use priority inheritance on mutex
	 */
	if (pthread_mutexattr_init(&mutexattr) < 0) {
		pr_fail("pthread_mutexattr_init failed, errno=%d (%s)\n",
			errno, strerror(errno));
		(void)pthread_mutex_destroy(&prio_inv_info->mutex);
		return EXIT_FAILURE;
	}

	/* niceness for non-RR and non-FIFO scheduling */
	nice_max = 0;	/* normal level */
	nice_min = 19;	/* very low niceness */
	nice_div = (nice_min - nice_max) / (MUTEX_PROCS - 1);

	/* prio for RR and FIFO scheduling */
	prio_min = sched_get_priority_min(sched_policy);
	prio_max = sched_get_priority_max(sched_policy);
	prio_div = (prio_max - prio_min - 1) / (MUTEX_PROCS - 1);
	if (prio_div < 0)
		prio_div = 0;

	pthread_protocol = stress_prio_inv_types[prio_inv_type].value;
	if (pthread_protocol != -1)
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutexattr, pthread_protocol));

	VOID_RET(int, pthread_mutexattr_setprioceiling(&mutexattr, prio_max));
	VOID_RET(int, pthread_mutexattr_setrobust(&mutexattr, PTHREAD_MUTEX_ROBUST));
	if (pthread_mutex_init(&prio_inv_info->mutex, &mutexattr) < 0) {
		pr_fail("%s: pthread_mutex_init failed, errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < MUTEX_PROCS; i++) {
		pid_t pid;

		child_info[i].prio = prio_min + (prio_div * i);
		child_info[i].niceness = nice_max + (nice_div * i);
		child_info[i].usage = 0.0;

		pid = fork();
		if (pid < 0) {
			pr_inf("%s: cannot fork child process, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			rc = EXIT_NO_RESOURCE;
			goto reap;
		} else if (pid == 0) {
			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			if (stress_sighandler(args->name, SIGALRM, stress_prio_inv_alarm_handler, NULL) < 0)
				pr_inf("%s: cannot set SIGALRM signal handler, process termination may not work\n", args->name);

			child_info[i].pid = getpid();

			if (stress_prio_inv_set_prio_policy(args, child_info[i].prio, child_info[i].niceness, sched_policy) < 0)
				_exit(EXIT_FAILURE);
			stress_prio_inv_funcs[i](i, prio_inv_info);

			(void)kill(ppid, SIGALRM);
			_exit(0);
		} else {
			child_info[i].pid = pid;
		}
	}

	if (stress_prio_inv_set_prio_policy(args, prio_max, nice_max, sched_policy) < 0) {
		rc = EXIT_FAILURE;
		goto reap;
	}

	(void)stress_prio_inv_set_prio_policy(args, prio_max, 0, sched_policy);

	/* Wait for termination */
	while (stress_continue(args))
		(void)shim_usleep(250000);

reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/* Need to send alarm to all children before waitpid'ing them */
	for (i = 0; i < MUTEX_PROCS; i++) {
		if (child_info[i].pid != -1) {
			stress_force_killed_bogo(args);
			(void)shim_kill(child_info[i].pid, SIGALRM);
		}
	}
	/* Now wait for children to exit */
	for (i = 0; i < MUTEX_PROCS; i++) {
		if (child_info[i].pid != -1) {
			int status;

			(void)shim_waitpid(child_info[i].pid, &status, 0);
		}
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

	if ((pthread_protocol >= 0) &&
	    (pthread_protocol == STRESS_PRIO_INV_TYPE_INHERIT) &&
	    (child_info[2].usage < child_info[0].usage * 0.9) &&
	    (child_info[0].usage > 1.0)) {
		pr_warn("%s: mutex priority inheritance appears incorrect, "
			"low priority process has far more run time "
			"(%.2f secs) than high priority process (%.2f secs)\n",
			args->name, child_info[0].usage, child_info[2].usage);
	}

	(void)pthread_mutex_destroy(&prio_inv_info->mutex);
#if !defined(SCHED_OTHER)
unmap_prio_inv_info:
#endif
	(void)munmap((void *)prio_inv_info, sizeof(*prio_inv_info));

	return rc;
}

const stressor_info_t stress_prio_inv_info = {
	.stressor = stress_prio_inv,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_prio_inv_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_SCHEDULER,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without librt, pthread_np.h, pthread or SCHED_* support"
};
#endif
