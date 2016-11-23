/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) && \
     !defined(__OpenBSD__) && !defined(__minix__)
#include <sched.h>

#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
struct __sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flags;
	int32_t  sched_nice;
	uint32_t sched_priority;
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
};
#endif

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

#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
static inline int sys_sched_getattr(
	pid_t pid,
	struct __sched_attr *attr,
	unsigned int size,
	unsigned int flags)
{
	return syscall(__NR_sched_getattr, pid, attr, size, flags);
}

static inline int sys_sched_setattr(
	pid_t pid,
	struct __sched_attr *attr,
	unsigned int flags)
{
	return syscall(__NR_sched_setattr, pid, attr, flags);
}
#endif

int stress_schedpolicy(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int policy = 0;
	const pid_t mypid = getpid();

	if (SIZEOF_ARRAY(policies) == 0) {
		if (instance == 0) {
			pr_inf(stderr, "%s: no scheduling policies "
				"available, skipping test\n",
				name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}

	do {
#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
		struct __sched_attr attr;
#endif
		struct sched_param param, new_param;
		int ret = 0;
		int max_prio, min_prio, rng_prio;
		int new_policy = policies[policy];
		const pid_t pid = (mwc32() & 1) ? 0 : mypid;
		const char *new_policy_name = get_sched_name(new_policy);
		bool set_ok = false;

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
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
			min_prio = sched_get_priority_min(policy);
			max_prio = sched_get_priority_max(policy);
			rng_prio = max_prio - min_prio;
			if (rng_prio == 0) {
				pr_err(stderr, "%s: invalid min/max priority "
					"range for scheduling policy %s "
					"(min=%d, max=%d)\n",
					name,
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
				pr_fail(stderr, "%s: sched_setscheduler "
					"failed: errno=%d (%s) "
					"for scheduler policy %s\n",
					name, errno, strerror(errno),
					new_policy_name);
			}
		} else {
			ret = sched_getscheduler(pid);
			if (ret < 0) {
				pr_fail_err(name, "sched_getscheduler");
			} else if (ret != policies[policy]) {
				pr_fail(stderr, "%s: sched_getscheduler "
					"failed: pid %d has policy %d (%s) "
					"but function returned %d instead\n",
					name, pid, new_policy,
					new_policy_name, ret);
			} else {
				set_ok = true;
			}
		}
#if defined(_POSIX_PRIORITY_SCHEDULING)
		ret = sched_getparam(pid, &new_param);
		if (ret < 0) {
			pr_fail_err(name, "sched_getparam failed");
		} else if (set_ok &&
			   (param.sched_priority != new_param.sched_priority)) {
			pr_fail(stderr, "%s: sched_getparam failed, set "
				"sched_priority %d is not the same as "
				"the fetched sched_priority %d\n",
				name, param.sched_priority,
				new_param.sched_priority);
		}

		ret = sched_setparam(pid, &new_param);
		if (ret < 0) {
			pr_fail_err(name, "sched_setparam");
		}
#endif

#if defined(__linux__) && \
    defined(__NR_sched_getattr) && \
    defined(__NR_sched_setattr)
		/*
		 *  Nothing too clever here, just get and set for now
		 */
		memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
		ret = sys_sched_getattr(pid, &attr, sizeof(attr), 0);
		if (ret < 0) {
			if (errno != ENOSYS) {
				pr_fail_err(name, "sched_getattr");
			}
		}

		attr.size = sizeof(attr);
		ret = sys_sched_setattr(pid, &attr, 0);
		if (ret < 0) {
			if (errno != ENOSYS) {
				pr_fail_err(name, "sched_getattr");
			}
		}
#endif

#if defined(_POSIX_PRIORITY_SCHEDULING)
		sched_yield();
#endif
		policy++;
		policy %= SIZEOF_ARRAY(policies);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#else
int stress_schedpolicy(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_not_implemented(counter, instance, max_ops, name);
}
#endif
