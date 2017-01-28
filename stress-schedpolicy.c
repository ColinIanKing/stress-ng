/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && \
     !defined(__OpenBSD__) && !defined(__minix__)

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
};

int stress_schedpolicy(const args_t *args)
{
	int policy = 0;

	if (SIZEOF_ARRAY(policies) == 0) {
		if (args->instance == 0) {
			pr_inf("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}

	do {
#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
		struct shim_sched_attr attr;
#endif
		struct sched_param param;
		int ret = 0;
		int max_prio, min_prio, rng_prio;
		int new_policy = policies[policy];
		const pid_t pid = (mwc32() & 1) ? 0 : args->pid;
		const char *new_policy_name = get_sched_name(new_policy);

		switch (new_policy) {
#if defined(SCHED_IDLE)
		case SCHED_IDLE:
#endif
#if defined(SCHED_BATCH)
		case SCHED_BATCH:
#endif
#if defined(SCHED_OTHER)
		case SCHED_OTHER:
#endif
			param.sched_priority = 0;
			ret = sched_setscheduler(pid, new_policy, &param);
			break;
#if defined(SCHED_RR)
		case SCHED_RR:
#if defined(_POSIX_PRIORITY_SCHEDULING)
			{
				struct timespec t;

				ret = sched_rr_get_interval(pid, &t);
				(void)ret;
			}
#endif
			CASE_FALLTHROUGH;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
			min_prio = sched_get_priority_min(policy);
			max_prio = sched_get_priority_max(policy);
			rng_prio = max_prio - min_prio;
			if (rng_prio == 0) {
				pr_err("%s: invalid min/max priority "
					"range for scheduling policy %s "
					"(min=%d, max=%d)\n",
					args->name,
					new_policy_name,
					min_prio, max_prio);
				break;
			}
			param.sched_priority = (mwc32() % (rng_prio)) +
						min_prio;
			ret = sched_setscheduler(pid, new_policy, &param);
			break;
		default:
			/* Should never get here */
			break;
		}
		if (ret < 0) {
			if (errno != EPERM) {
				pr_fail("%s: sched_setscheduler "
					"failed: errno=%d (%s) "
					"for scheduler policy %s\n",
					args->name, errno, strerror(errno),
					new_policy_name);
			}
		} else {
			ret = sched_getscheduler(pid);
			if (ret < 0) {
				pr_fail_err("sched_getscheduler");
			} else if (ret != policies[policy]) {
				pr_fail("%s: sched_getscheduler "
					"failed: pid %d has policy %d (%s) "
					"but function returned %d instead\n",
					args->name, (int)pid, new_policy,
					new_policy_name, ret);
			}
		}
#if defined(_POSIX_PRIORITY_SCHEDULING)
		memset(&param, 0, sizeof param);
		ret = sched_getparam(pid, &param);
		if (ret < 0)
			pr_fail_err("sched_getparam failed");

		ret = sched_setparam(pid, &param);
		if (ret < 0)
			pr_fail_err("sched_setparam");
#endif

#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
		/*
		 *  Nothing too clever here, just get and set for now
		 */
		memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		ret = shim_sched_getattr(pid, &attr, sizeof(attr), 0);
		if (ret < 0) {
			if (errno != ENOSYS) {
				pr_fail_err("sched_getattr");
			}
		}

		attr.size = sizeof(attr);
		ret = shim_sched_setattr(pid, &attr, 0);
		if (ret < 0) {
			if (errno != ENOSYS) {
				pr_fail_err("sched_getattr");
			}
		}
#endif

		(void)shim_sched_yield();
		policy++;
		policy %= SIZEOF_ARRAY(policies);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_schedpolicy(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
