/*
 * Copyright (C)      2023 Colin Ian King.
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
#include "core-capabilities.h"

#if defined(__NR_sched_getattr)
#define HAVE_SCHED_GETATTR
#endif

#if defined(__NR_sched_setattr)
#define HAVE_SCHED_SETATTR
#endif

#define SCHED_PROCS_MAX	(16)

static const stress_help_t help[] = {
	{ NULL,	"schedmix N",		"start N workers that exercise a mix of scheduling loads" },
	{ NULL,	"schedmix-ops N",	"stop after N schedmix bogo operations" },
	{ NULL,	NULL,			NULL }
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

static inline void stress_schedmix_nop(void)
{
#if defined(HAVE_ASM_NOP)
#if defined(STRESS_ARCH_KVX)
        /*
         * Extra ;; required for KVX to indicate end of
         * a VLIW instruction bundle
         */
        __asm__ __volatile__("nop\n;;\n");
#else
        __asm__ __volatile__("nop;\n");
#endif
#else
	shim_mb();
#endif
}

static inline void stress_schedmix_waste_time(const stress_args_t *args)
{
	int i, n;

	n = stress_mwc8modn(19);
	switch (n) {
	case 0:
		shim_sched_yield();
		break;
	case 1:
		n = stress_mwc16();
		for (i = 0; stress_continue(args) && (i < n); i++)
			shim_sched_yield();
		break;
	case 2:
		shim_nanosleep_uint64(stress_mwc32modn(1000000));
		break;
	case 3:
		n = stress_mwc8();
		for (i = 0; stress_continue(args) && (i < n); i++)
			shim_nanosleep_uint64(stress_mwc32modn(10000));
		break;
	case 4:
		for (i = 0; stress_continue(args) && (i < 1000000); i++)
			stress_schedmix_nop();
		break;
	case 5:
		n = stress_mwc32modn(1000000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			stress_schedmix_nop();
		break;
	case 6:
		for (i = 0; stress_continue(args) && (i < 10000); i++)
			VOID_RET(double, stress_time_now());
		break;
	case 7:
		n = stress_mwc16modn(10000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			VOID_RET(double, stress_time_now());
		break;
	case 8:
		for (i = 0; stress_continue(args) && (i < 1000); i++)
			VOID_RET(int, nice(0));
		break;
	case 9:
		n = stress_mwc16modn(1000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			VOID_RET(int, nice(0));
		break;
	case 10:
		for (i = 0; stress_continue(args) && (i < 10); i++)
			VOID_RET(uint64_t, stress_get_prime64(stress_mwc32()));
		break;
	case 11:
		for (i = 0; stress_continue(args) && (i < 1000); i++)
			getpid();
		break;
	case 12:
		n = stress_mwc16modn(1000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			getpid();
		break;
	case 13:
		for (i = 0; stress_continue(args) && (i < 1000); i++)
			sleep(0);
		break;
	case 14:
		n = stress_mwc16modn(1000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			sleep(0);
		break;
	case 15:
		n = stress_mwc16modn(1000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			getpid();
		break;
	case 16:
		getpid();
		break;
	case 17:
		VOID_RET(int, shim_usleep_interruptible(10000));
		break;
	case 18:
		break;
	}
}

static int stress_schedmix_child(const stress_args_t *args)
{
	int policy = args->instance % SIZEOF_ARRAY(policies);
	int old_policy = -1;

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
		do {
			policy = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(policies));
		} while (policy == old_policy);
		old_policy = policy;

		new_policy = policies[policy];
		new_policy_name = stress_get_sched_name(new_policy);

		if (!stress_continue(args))
			break;

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
		}
		stress_schedmix_waste_time(args);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

static int stress_schedmix(const stress_args_t *args)
{
	pid_t pids[SCHED_PROCS_MAX];
	size_t i;
	const int parent_cpu = stress_get_cpu();

	if (SIZEOF_ARRAY(policies) == (0)) {
		if (args->instance == 0) {
			pr_inf_skip("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < SIZEOF_ARRAY(pids); i++) {
		stress_mwc_reseed();

		pids[i] = fork();
		if (pids[i] < 0) {
			continue;
		} else if (pids[i] == 0) {
			stress_parent_died_alarm();
			(void)stress_change_cpu(args, parent_cpu);
			_exit(stress_schedmix_child(args));
		}
	}

	do {
		pause();
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return stress_kill_and_wait_many(args, pids, SIZEOF_ARRAY(pids), SIGALRM, true);
}

stressor_info_t stress_schedmix_info = {
	.stressor = stress_schedmix,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_schedmix_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
