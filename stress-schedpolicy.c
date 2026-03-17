/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-sched.h"

#include <sched.h>

static const stress_help_t help[] = {
	{ NULL,	"schedpolicy N",	"start N workers that exercise scheduling policy" },
	{ NULL,	"schedpolicy-ops N",	"stop after N scheduling policy bogo operations" },
	{ NULL, "schedpolicy-rand",	"select scheduling policy randomly" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_schedpolicy_rand,	"schedpolicy-rand", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__HAIKU__) &&						\
     !defined(__serenity__)

static int stress_schedpolicy(stress_args_t *args)
{
	size_t policy_index = (size_t)args->instance % stress_sched_types_length;
	int new_policy = stress_sched_types[policy_index].sched;
	int old_policy = -1, rc = EXIT_SUCCESS;
	bool schedpolicy_rand = false;
#if defined(_POSIX_PRIORITY_SCHEDULING)
	const bool root_or_nice_capability = stress_capabilities_check(SHIM_CAP_SYS_NICE);
#endif
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
	const double runtime = 0.90 * STRESS_DBL_NANOSECOND / (double)args->instances;
	const double deadline = 0.95 * STRESS_DBL_NANOSECOND / (double)args->instances;
	const double period = 1.00 * STRESS_DBL_NANOSECOND / (double)args->instances;
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
	size_t i;
	double t_start, duration;
	const pid_t pid = getpid();
	uint64_t *counters;
	const bool cap_sys_nice = stress_capabilities_check(SHIM_CAP_SYS_NICE);

#if defined(SCHED_FLAG_DL_OVERRUN) &&	\
    defined(SIGXCPU)
	if (stress_signal_handler(args->name, SIGXCPU, stress_signal_ignore_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif

	counters = (uint64_t *)calloc(stress_sched_types_length, sizeof(*counters));
	if (!counters) {
		pr_inf_skip("%s: cannot allocate %zu 64 bit counters, skipping stressor\n",
			args->name, stress_sched_types_length);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_setting_get("schedpolicy-rand", &schedpolicy_rand);

	if (stress_sched_types_length == (0)) {
		if (stress_instance_zero(args)) {
			pr_inf_skip("%s: no scheduling policies "
				"available, skipping stressor\n",
				args->name);
		}
		free(counters);
		return EXIT_NOT_IMPLEMENTED;
	}

	for (i = 0; i < stress_sched_types_length; i++)
		counters[i] = 0;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	do {
#if defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
		struct shim_sched_attr attr;
#else
		UNEXPECTED
#endif
		struct sched_param param;
		int ret = 0;
		int max_prio, min_prio, rng_prio;
		const char *new_policy_name;

		/*
		 *  find a new randomized policy that is not the same
		 *  as the previous old policy
		 */
		if (schedpolicy_rand) {
			do {
				policy_index = stress_mwcsizemodn(stress_sched_types_length);
				new_policy = stress_sched_types[policy_index].sched;
			} while (new_policy == old_policy);
			old_policy = new_policy;
		} else {
			new_policy = stress_sched_types[policy_index].sched;
			old_policy = new_policy;
		}
		new_policy_name = stress_sched_types[policy_index].sched_macro_name;

		if (UNLIKELY(!stress_continue(args)))
			break;

		errno = 0;

		switch (new_policy) {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
		case SCHED_DEADLINE:
			(void)shim_memset(&param, 0, sizeof(param));
			param.sched_priority = 0;
#if defined(SCHED_OTHER)
			(void)sched_setscheduler(pid, SCHED_OTHER, &param);
#else
			(void)sched_setscheduler(pid, new_policy, &param);
#endif

			(void)shim_memset(&attr, 0, sizeof(attr));
			attr.size = sizeof(attr);
			attr.sched_nice = 0;
			attr.sched_priority = 0;
			attr.sched_policy = SCHED_DEADLINE;
			attr.sched_flags = 0;
#if defined(SCHED_FLAG_DL_OVERRUN) &&	\
    defined(SIGXCPU)
			if (stress_mwc1())
				attr.sched_flags |= SCHED_FLAG_DL_OVERRUN;
#endif
#if defined(SCHED_FLAG_RESET_ON_FORK)
			if (stress_mwc1())
				attr.sched_flags |= SCHED_FLAG_RESET_ON_FORK;
#endif
#if defined(SCHED_FLAG_RECLAIM)
			if (stress_mwc1())
				attr.sched_flags |= SCHED_FLAG_RECLAIM;
#endif
#if defined(SCHED_FLAG_UTIL_CLAMP_MIN)
			if (stress_mwc1()) {
				attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
				attr.sched_util_min = 1;
			}
#endif
#if defined(SCHED_FLAG_UTIL_CLAMP_MAX)
			if (stress_mwc1()) {
				attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MAX;
				attr.sched_util_max = 2 + (stress_mwc8() & 0x3f);
			}
#endif
			attr.sched_runtime = (uint64_t)runtime;
			if (attr.sched_runtime < 1024)
				attr.sched_runtime = 1024;
			attr.sched_deadline = (uint64_t)deadline;
			if (attr.sched_deadline < 1050)
				attr.sched_deadline = 1050;
			attr.sched_period = (uint64_t)period;
			if (attr.sched_period < 1100)
				attr.sched_period = 1100;

			ret = shim_sched_setattr(pid, &attr, 0);
			break;
#endif

#if defined(SCHED_BATCH)
		case SCHED_BATCH:
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

#if defined(HAVE_SCHED_SETATTR)
			if (cap_sys_nice && stress_mwc1()) {
				(void)shim_memset(&attr, 0, sizeof(attr));
				attr.size = sizeof(attr);
				attr.sched_policy = new_policy;
				attr.sched_nice = stress_mwc8modn(40) - 19;
				attr.sched_flags = 0;
#if defined(SCHED_FLAG_UTIL_CLAMP_MIN)
				if (stress_mwc1()) {
					attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MIN;
					attr.sched_util_min = 1;
				}
#endif
#if defined(SCHED_FLAG_UTIL_CLAMP_MAX)
				if (stress_mwc1()) {
					attr.sched_flags |= SCHED_FLAG_UTIL_CLAMP_MAX;
					attr.sched_util_max = 2 + (stress_mwc8() & 0x3f);
				}
#endif
				errno = 0;
				ret = shim_sched_setattr(pid, &attr, 0);
				break;
			}
#endif
			param.sched_priority = 0;
			errno = 0;
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
			goto case_sched_fifo;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
case_sched_fifo:
			min_prio = sched_get_priority_min(new_policy);
			max_prio = sched_get_priority_max(new_policy);

			/* Check if min/max is supported or not */
			if (UNLIKELY((min_prio == -1) || (max_prio == -1)))
				continue;

			rng_prio = max_prio - min_prio;
			if (UNLIKELY(rng_prio <= 0)) {
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
			if (UNLIKELY((errno != EPERM) &&
				     (errno != EINVAL) &&
				     (errno != EINTR) &&
				     (errno != ENOSYS) &&
				     (errno != EBUSY))) {
				pr_fail("%s: sched_setscheduler "
					"failed, errno=%d (%s) "
					"for scheduler policy %s\n",
					args->name, errno, strerror(errno),
					new_policy_name);
				rc = EXIT_FAILURE;
				break;
			}
		} else {
			for (i = 0; i < stress_sched_types_length; i++) {
				if ((stress_sched_types[i].sched) == new_policy) {
					counters[i]++;
					break;
				}
			}

			if (stress_sched_types[policy_index].check_getscheduler) {
				ret = sched_getscheduler(pid) & 0xff;
				if (UNLIKELY(ret < 0)) {
					pr_fail("%s: sched_getscheduler failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				} else if (UNLIKELY(ret != new_policy)) {
					pr_fail("%s: sched_getscheduler "
						"failed, PID %" PRIdMAX " has policy %d (%s) "
						"but sched_getscheduler returned %d (%s) instead\n",
						args->name, (intmax_t)getpid(), new_policy,
						new_policy_name, ret,
						stress_sched_name_get(ret));
					rc = EXIT_FAILURE;
					break;
				}
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
			VOID_RET(int, sched_getparam(pid, (struct sched_param *)stress_null_get()));
#endif

			/* Exercise bad pid, ESRCH error */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_getparam(stress_unused_racy_pid_get(false), &param));

			/* Exercise invalid sched_setparam syscall */
			(void)shim_memset(&param, 0, sizeof(param));
			VOID_RET(int, sched_setparam(-1, &param));

#if defined(__linux__)
			/* Linux allows NULL param, will return EFAULT */
			VOID_RET(int, sched_setparam(pid, (struct sched_param *)stress_null_get()));
#endif

			/*
			 * Exercise bad pid, ESRCH error only if process does not
			 * root or nice capability (to avoid clobbering processes we
			 * don't own
			 */
			if (!root_or_nice_capability) {
				VOID_RET(int, sched_setparam(stress_unused_racy_pid_get(false), &param));
			}
		}
		/* Exercise with invalid PID */
		VOID_RET(int, sched_getscheduler(-1));

		/* Exercise with bad pid, ESRCH error */
		VOID_RET(int, sched_getscheduler(stress_unused_racy_pid_get(false)));

		(void)shim_memset(&param, 0, sizeof(param));
		ret = sched_getparam(pid, &param);
		if (UNLIKELY((ret < 0) && ((errno != EINVAL) && (errno != EPERM)))) {
			pr_fail("%s: sched_getparam failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		ret = sched_setparam(pid, &param);
		if (UNLIKELY((ret < 0) && ((errno != EINVAL) && (errno != EPERM)))) {
			pr_fail("%s: sched_setparam failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
#endif

#if defined(HAVE_SCHED_GETATTR) && \
    defined(HAVE_SCHED_SETATTR)
		/* Exercise too large attr > page size */
		{
			const size_t large_attr_size = args->page_size + 16;
			char *large_attr;

			large_attr = (char *)calloc(large_attr_size, sizeof(*large_attr));
			if (large_attr) {
				(void)shim_memset(large_attr, 0, large_attr_size);

				VOID_RET(int, shim_sched_getattr(pid,
					(struct shim_sched_attr *)(void *)large_attr,
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
		VOID_RET(int, shim_sched_getattr(stress_unused_racy_pid_get(false),
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
				rc = EXIT_FAILURE;
				break;
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
		if (UNLIKELY(ret < 0)) {
			if (errno != ENOSYS) {
				pr_fail("%s: sched_setattr failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				break;
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
		policy_index++;
		if (UNLIKELY(policy_index >= stress_sched_types_length))
			policy_index = 0;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	duration = stress_time_now() - t_start;
	if (duration > 0.0) {
		for (i = 0; i < stress_sched_types_length; i++) {
			if (counters[i] > 0) {
				const double rate = counters[i] / duration;
				char buf[64];

				(void)snprintf(buf, sizeof(buf), "%s schedules per sec", stress_sched_types[i].sched_macro_name);
				stress_metrics_set(args, buf, rate, STRESS_METRIC_GEOMETRIC_MEAN);
			}
		}
	}

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	free(counters);

	return rc;
}

const stressor_info_t stress_schedpolicy_info = {
	.stressor = stress_schedpolicy,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_schedpolicy_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
