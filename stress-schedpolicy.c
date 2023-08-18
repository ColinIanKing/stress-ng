// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"

#if defined(__NR_sched_getattr)
#define HAVE_SCHED_GETATTR
#endif

#if defined(__NR_sched_setattr)
#define HAVE_SCHED_SETATTR
#endif

static const stress_help_t help[] = {
	{ NULL,	"schedpolicy N",	"start N workers that exercise scheduling policy" },
	{ NULL,	"schedpolicy-ops N",	"stop after N scheduling policy bogo operations" },
	{ NULL, "schedpolicy-rand",	"select scheduling policy randomly" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_schedpolicy_rand(const char *opt)
{
	return stress_set_setting_true("schedpolicy-rand", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_schedpolicy_rand,	stress_set_schedpolicy_rand },
	{ 0,			NULL },
};

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__serenity__)

static const int policies[] = {
#if defined(SCHED_IDLE)
	SCHED_IDLE,
#endif
#if defined(SCHED_FIFO)
	SCHED_FIFO,
#endif
#if defined(SCHED_RR)
	SCHED_RR,
#endif
#if defined(SCHED_OTHER)
	SCHED_OTHER,
#endif
#if defined(SCHED_BATCH)
	SCHED_BATCH,
#endif
#if defined(SCHED_DEADLINE)
	SCHED_DEADLINE,
#endif
};

static int stress_schedpolicy(const stress_args_t *args)
{
	int policy = args->instance % SIZEOF_ARRAY(policies);
	int old_policy = -1;
	bool schedpolicy_rand = false;
#if defined(_POSIX_PRIORITY_SCHEDULING)
	const bool root_or_nice_capability = stress_check_capability(SHIM_CAP_SYS_NICE);
#endif
#if defined(HAVE_SCHED_GETATTR) && \
    defined(HAVE_SCHED_SETATTR)
	uint32_t sched_util_min = ~0U;
	uint32_t sched_util_max = 0;
	uint32_t sched_util_max_value = 0;
	int counter = 0;
#endif

#if defined(_POSIX_PRIORITY_SCHEDULING)
	int n = 0;
#endif

	(void)stress_get_setting("schedpolicy-rand", &schedpolicy_rand);

	if (SIZEOF_ARRAY(policies) == (0)) {
		if (args->instance == 0) {
			pr_inf_skip("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
		struct shim_sched_attr attr;
#else
		UNEXPECTED
#endif
		struct sched_param param;
		int ret = 0;
		int max_prio, min_prio, rng_prio, new_policy;
		const pid_t pid = stress_mwc1() ? 0 : args->pid;
		const char *new_policy_name;

		/*
		 *  find a new randomized policy that is not the same
		 *  as the previous old policy
		 */
		if (schedpolicy_rand) {
			do {
				policy = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(policies));
			} while (policy == old_policy);
			old_policy = policy;
		}

		new_policy = policies[policy];
		new_policy_name = stress_get_sched_name(new_policy);

		if (!stress_continue(args))
			break;

		shim_sched_yield();
		errno = 0;

		switch (new_policy) {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
		case SCHED_DEADLINE:
			/*
			 *  Only have 1 RT deadline instance running
			 */
			if (args->instance == 0) {
				(void)shim_memset(&attr, 0, sizeof(attr));
				attr.size = sizeof(attr);
				attr.sched_flags = 0;
				attr.sched_nice = 0;
				attr.sched_priority = 0;
				attr.sched_policy = SCHED_DEADLINE;
				/* runtime <= deadline <= period */
				attr.sched_runtime = 64 * 1000000;
				attr.sched_deadline = 128 * 1000000;
				attr.sched_period = 256 * 1000000;

				ret = shim_sched_setattr(0, &attr, 0);
				break;
			}
			CASE_FALLTHROUGH;
#endif

#if defined(SCHED_IDLE)
		case SCHED_IDLE:
			CASE_FALLTHROUGH;
#endif
#if defined(SCHED_BATCH)
		case SCHED_BATCH:
			CASE_FALLTHROUGH;
#endif
#if defined(SCHED_OTHER)
		case SCHED_OTHER:
#endif
			/* Exercise illegal policy */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_setscheduler(pid, -1, &param));

			/* Exercise invalid PID */
			param.sched_priority = 0;
			VOID_RET(int, sched_setscheduler(-1, new_policy, &param));

			/* Exercise invalid priority */
			param.sched_priority = ~0;
			VOID_RET(int, sched_setscheduler(pid, new_policy, &param));

			param.sched_priority = 0;
			ret = sched_setscheduler(pid, new_policy, &param);

			break;
#if defined(SCHED_RR)
		case SCHED_RR:
#if defined(HAVE_SCHED_RR_GET_INTERVAL)
			{
				struct timespec t;

				VOID_RET(int, sched_rr_get_interval(pid, &t));
			}
#endif
			CASE_FALLTHROUGH;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
			min_prio = sched_get_priority_min(new_policy);
			max_prio = sched_get_priority_max(new_policy);

			/* Check if min/max is supported or not */
			if ((min_prio == -1) || (max_prio == -1))
				continue;

			rng_prio = max_prio - min_prio;
			if (UNLIKELY(rng_prio == 0)) {
				pr_err("%s: invalid min/max priority "
					"range for scheduling policy %s "
					"(min=%d, max=%d)\n",
					args->name,
					new_policy_name,
					min_prio, max_prio);
				break;
			}
			param.sched_priority = (int)stress_mwc32modn(rng_prio) + min_prio;
			ret = sched_setscheduler(pid, new_policy, &param);
			break;
		default:
			/* Should never get here */
			break;
		}
		if (ret < 0) {
			/*
			 *  Some systems return EINVAL for non-POSIX
			 *  scheduling policies, silently ignore these
			 *  failures.
			 */
			if ((errno != EPERM) &&
			    (errno != EINVAL) &&
			    (errno != EINTR) &&
			    (errno != ENOSYS) &&
			    (errno != EBUSY)) {
				pr_fail("%s: sched_setscheduler "
					"failed: errno=%d (%s) "
					"for scheduler policy %s\n",
					args->name, errno, strerror(errno),
					new_policy_name);
			}
		} else {
			ret = sched_getscheduler(pid);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: sched_getscheduler failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			} else if (ret != policies[policy]) {
				pr_fail("%s: sched_getscheduler "
					"failed: pid %d has policy %d (%s) "
					"but function returned %d instead\n",
					args->name, (int)pid, new_policy,
					new_policy_name, ret);
			}
		}
#if defined(_POSIX_PRIORITY_SCHEDULING)
		if (UNLIKELY(n++ >= 1024)) {
			n = 0;

			/* Exercise invalid sched_getparam syscall */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_getparam(-1, &param));

#if defined(__linux__)
			/* Linux allows NULL param, will return EFAULT */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_getparam(pid, NULL));
#endif

			/* Exercise bad pid, ESRCH error */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_getparam(stress_get_unused_pid_racy(false), &param));

			/* Exercise invalid sched_setparam syscall */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_setparam(-1, &param));

#if defined(__linux__)
			/* Linux allows NULL param, will return EFAULT */
			VOID_RET(int, sched_setparam(pid, NULL));
#endif

			/*
			 * Exercise bad pid, ESRCH error only if process does not
			 * root or nice capability (to avoid clobbering processes we
			 * don't own
			 */
			if (!root_or_nice_capability) {
				VOID_RET(int, sched_setparam(stress_get_unused_pid_racy(false), &param));
			}
		}
		/* Exercise with invalid PID */
		VOID_RET(int, sched_getscheduler(-1));

		/* Exercise with bad pid, ESRCH error */
		VOID_RET(int, sched_getscheduler(stress_get_unused_pid_racy(false)));

		(void)shim_memset(&param, 0, sizeof(param));
		ret = sched_getparam(pid, &param);
		if (UNLIKELY((ret < 0) && ((errno != EINVAL) && (errno != EPERM))))
			pr_fail("%s: sched_getparam failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

		ret = sched_setparam(pid, &param);
		if (UNLIKELY((ret < 0) && ((errno != EINVAL) && (errno != EPERM))))
			pr_fail("%s: sched_setparam failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
#endif

#if defined(HAVE_SCHED_GETATTR) && \
    defined(HAVE_SCHED_SETATTR)
		/* Exercise too large attr > page size */
		{
			const size_t large_attr_size = args->page_size + 16;
			char *large_attr;

			large_attr = calloc(large_attr_size, sizeof(*large_attr));
			if (large_attr) {
				(void)shim_memset(large_attr, 0, large_attr_size);

				VOID_RET(int, shim_sched_getattr(pid,
					(struct shim_sched_attr *)large_attr,
					(unsigned int)large_attr_size, 0));

				free(large_attr);
			}
		}

		/* Exercise invalid sched_getattr syscalls */
		(void)shim_memset(&attr, 0, sizeof(attr));
		VOID_RET(int, shim_sched_getattr(pid, &attr, sizeof(attr), ~0U));

		/* Exercise -ve pid */
		(void)shim_memset(&attr, 0, sizeof(attr));
		VOID_RET(int, shim_sched_getattr(-1, &attr, sizeof(attr), 0));

		/* Exercise bad pid, ESRCH error */
		(void)shim_memset(&attr, 0, sizeof(attr));
		VOID_RET(int, shim_sched_getattr(stress_get_unused_pid_racy(false),
			&attr, sizeof(attr), 0));

		/*
		 *  Nothing too clever here, just get and set for now
		 */
		(void)shim_memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		ret = shim_sched_getattr(pid, &attr, sizeof(attr), 0);
		if (UNLIKELY(ret < 0)) {
			if (UNLIKELY(errno != ENOSYS)) {
				pr_fail("%s: sched_getattr failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}

		if (attr.sched_util_max != 0) {
			/*
			 *  Newer Linux kernels support a min/max setting,
			 *  and if supported then find the min/max settings
			 */
			if (sched_util_min > attr.sched_util_min)
				sched_util_min = attr.sched_util_min;
			if (sched_util_max < attr.sched_util_max)
				sched_util_max = attr.sched_util_max;
			/* Sanity check */
			if (sched_util_min > sched_util_max)
				sched_util_min = sched_util_max;

			/* Zero value means not initialized yet */
			if (sched_util_max_value == 0)
				sched_util_max_value = sched_util_max;

			attr.sched_util_max = sched_util_max_value;
		}

		/*
		 * Exercise invalid sched_getattr syscalls, even if the
		 * syscalls succeed only correct value will be set,
		 * hence ignoring whether syscall succeeds or fails
		 */
		VOID_RET(int, shim_sched_setattr(pid, &attr, ~0U));
		VOID_RET(int, shim_sched_setattr(-1, &attr, 0));

		attr.size = sizeof(attr);
		ret = shim_sched_setattr(pid, &attr, 0);
		if (ret < 0) {
			if (errno != ENOSYS) {
				pr_fail("%s: sched_setattr failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}

		/*
		 * Cycle down max value to min value to
		 * exercise scheduler
		 */
		if (UNLIKELY((counter++ > 256)) &&
		    (sched_util_max_value > 0) &&
		    (sched_util_max_value > sched_util_min)) {
			sched_util_max_value--;
			counter = 0;
		}
#else
		UNEXPECTED
#endif
		policy++;
		if (policy >= (int)SIZEOF_ARRAY(policies))
			policy = 0;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_schedpolicy_info = {
	.stressor = stress_schedpolicy,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_schedpolicy_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
