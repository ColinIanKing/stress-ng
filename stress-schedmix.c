/*
 * Copyright (C) 2023-2024 Colin Ian King.
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
#include "core-killpid.h"

#include <sched.h>
#include <sys/times.h>

#if defined(HAVE_LINUX_MEMBARRIER_H)
#include <linux/membarrier.h>
#endif
#if defined(HAVE_SEMAPHORE_H)
#include <semaphore.h>
#endif

#if defined(__NR_sched_getattr)
#define HAVE_SCHED_GETATTR
#endif

#if defined(__NR_sched_setattr)
#define HAVE_SCHED_SETATTR
#endif

#if defined(HAVE_SEMAPHORE_H) && \
    defined(HAVE_LIB_PTHREAD) && \
    defined(HAVE_SEM_POSIX) &&	 \
    defined(CLOCK_REALTIME)
#define HAVE_SCHEDMIX_SEM
#endif

#define MIN_SCHEDMIX_PROCS	(1)
#define MAX_SCHEDMIX_PROCS	(64)
#define DEFAULT_SCHEDMIX_PROCS	(16)

static const stress_help_t help[] = {
	{ NULL,	"schedmix N",		"start N workers that exercise a mix of scheduling loads" },
	{ NULL,	"schedmix-ops N",	"stop after N schedmix bogo operations" },
	{ NULL, "schedmix-procs N",	"select number of schedmix child processes 1..64" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_schedmix_procs()
 *	set maximum number of processes allowed
 */
static int stress_set_schedmix_procs(const char *opt)
{
	size_t schedmix_procs;

	schedmix_procs = stress_get_uint32(opt);
	stress_check_range("schedmix-procs", (uint64_t)schedmix_procs,
		MIN_SCHEDMIX_PROCS, MAX_SCHEDMIX_PROCS);
	return stress_set_setting("schedmix-procs", TYPE_ID_SIZE_T, &schedmix_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_schedmix_procs,	stress_set_schedmix_procs },
        { 0,			NULL }
};

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	\
     !defined(__OpenBSD__) &&						\
     !defined(__minix__) &&						\
     !defined(__APPLE__) &&						\
     !defined(__serenity__)

#if defined(HAVE_SCHEDMIX_SEM)
typedef struct {
	sem_t sem;
	pid_t owner;
} stress_schedmix_sem_t;

static stress_schedmix_sem_t *schedmix_sem;
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
#if defined(SCHED_DEADLINE)
	SCHED_DEADLINE,
#endif
};

static inline void stress_schedmix_waste_time(stress_args_t *args)
{
	int i, n, status;
	pid_t pid;
	double min1, min5, min15;
#if defined(__linux__)
	char buf[256];
#endif
	struct tms tms_buf;
#if defined(HAVE_GETRUSAGE) &&	\
    (defined(RUSAGE_SELF) || defined(RUSAGE_CHILDREN))
	struct rusage usage;
#endif
#if defined(HAVE_SCHEDMIX_SEM)
	struct timespec timeout;
#endif

redo:
	n = stress_mwc8modn(25);
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
			stress_asm_nop();
		break;
	case 5:
		n = stress_mwc32modn(1000000);
		for (i = 0; stress_continue(args) && (i < n); i++)
			stress_asm_nop();
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
			VOID_RET(uint64_t, stress_get_prime64(stress_mwc8()));
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
		VOID_RET(int, shim_usleep_interruptible(1000));
		break;
	case 18:
#if defined(HAVE_LINUX_MEMBARRIER_H) &&	\
    defined(MEMBARRIER_CMD_GLOBAL)
		if (shim_membarrier(MEMBARRIER_CMD_GLOBAL, 0, 0) == 0)
			break;
#endif
		getpid();
		break;
	case 19:
		VOID_RET(int, stress_get_load_avg(&min1, &min5, &min15));
		break;
	case 20:
		pid = fork();
		if (pid == 0) {
			_exit(0);
		} else if (pid > 0) {
			VOID_RET(int, shim_waitpid(pid, &status, 0));
		}
		break;
	case 21:
#if defined(HAVE_GETRUSAGE)
#if defined(RUSAGE_SELF)
		VOID_RET(int, shim_getrusage(RUSAGE_SELF, &usage));
#endif
#if defined(RUSAGE_CHILDREN)
		VOID_RET(int, shim_getrusage(RUSAGE_CHILDREN, &usage));
#endif
#endif
		VOID_RET(int, times(&tms_buf));
		break;
#if defined(__linux__)
	case 22:
		VOID_RET(ssize_t, stress_system_read("/proc/pressure/cpu", buf, sizeof(buf)));
		break;
	case 23:
		VOID_RET(ssize_t, stress_system_read("/proc/self/schedstat", buf, sizeof(buf)));
		break;
#endif
#if defined(HAVE_SCHEDMIX_SEM)
	case 24:
		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0)
			break;
		timeout.tv_nsec += 1000000;
		if (timeout.tv_nsec > 1000000000) {
			timeout.tv_nsec -= 1000000000;
			timeout.tv_sec++;
		}
		if (sem_timedwait(&schedmix_sem->sem, &timeout) < 0) {
			/*
			 *  can't get semaphore, then stop/continue process
			 *  that currently holds it
			 */
			pid = schedmix_sem->owner;
			if (pid > 1) {
				(void)kill(pid, SIGSTOP);
				shim_sched_yield();
				(void)kill(pid, SIGCONT);
			}
		} else {
			/*
			 *  got semaphore, kill some cycles and release it
			 */
			schedmix_sem->owner = getpid();
			n = stress_mwc16();
			for (i = 0; stress_continue(args) && (i < n); i++)
				shim_sched_yield();
			schedmix_sem->owner = -1;
			(void)sem_post(&schedmix_sem->sem);
		}
		break;
#endif
	default:
		goto redo;
	}
}

#if defined(HAVE_SETITIMER) &&	\
    defined(ITIMER_PROF)
static inline void stress_schedmix_itimer_set(void)
{
	struct itimerval timer;

	timer.it_value.tv_sec = 0;
	timer.it_value.tv_usec = 10000 + stress_mwc32modn(10000);
	timer.it_interval.tv_sec = 0;
	timer.it_interval.tv_usec = timer.it_value.tv_usec;
	VOID_RET(int, setitimer(ITIMER_PROF, &timer, NULL));
}

static inline void stress_schedmix_itimer_clear(void)
{
	struct itimerval timer;

	(void)memset(&timer, 0, sizeof(timer));
	VOID_RET(int, setitimer(ITIMER_PROF, &timer, NULL));
}

static void stress_schedmix_itimer_handler(int signum)
{
	(void)signum;

	stress_schedmix_itimer_set();
}
#endif

static int stress_schedmix_child(stress_args_t *args)
{
	int old_policy = -1, rc = EXIT_SUCCESS;

#if defined(HAVE_SETITIMER) &&	\
    defined(ITIMER_PROF)
	if (stress_sighandler(args->name, SIGPROF, stress_schedmix_itimer_handler, NULL) == 0)
		stress_schedmix_itimer_set();
#endif

	do {
#if defined(SCHED_DEADLINE) &&		\
    defined(HAVE_SCHED_GETATTR) &&	\
    defined(HAVE_SCHED_SETATTR)
		struct shim_sched_attr attr;
#else
		UNEXPECTED
#endif
		struct sched_param param;
		int ret = 0, policy;
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
			goto case_sched_other;
#endif
#if defined(SCHED_IDLE)
		case SCHED_IDLE:
			goto case_sched_other;
#endif
#if defined(SCHED_BATCH)
		case SCHED_BATCH:
			goto case_sched_other;
#endif
#if defined(SCHED_OTHER)
		case SCHED_OTHER:
#endif
#if (defined(SCHED_DEADLINE) &&		\
     defined(HAVE_SCHED_GETATTR) &&	\
     defined(HAVE_SCHED_SETATTR)) ||	\
     defined(SCHED_IDLE) ||		\
     defined(SCHED_BATCH)
case_sched_other:
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
			goto case_sched_fifo;
#endif
#if defined(SCHED_FIFO)
		case SCHED_FIFO:
#endif
case_sched_fifo:
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
				rc = EXIT_FAILURE;
			}
		}
		stress_schedmix_waste_time(args);
		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(HAVE_SETITIMER) &&	\
    defined(ITIMER_PROF)
	stress_schedmix_itimer_clear();
#endif

	return rc;
}

static int stress_schedmix(stress_args_t *args)
{
	pid_t pids[MAX_SCHEDMIX_PROCS];
	size_t i;
	size_t schedmix_procs = DEFAULT_SCHEDMIX_PROCS;
	const int parent_cpu = stress_get_cpu();

	if (SIZEOF_ARRAY(policies) == (0)) {
		if (args->instance == 0) {
			pr_inf_skip("%s: no scheduling policies "
				"available, skipping test\n",
				args->name);
		}
		return EXIT_NOT_IMPLEMENTED;
	}

#if defined(HAVE_SCHEDMIX_SEM)
	schedmix_sem = (stress_schedmix_sem_t *)
		stress_mmap_populate(NULL, sizeof(*schedmix_sem),
			PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (schedmix_sem != MAP_FAILED) {
		if (sem_init(&schedmix_sem->sem, 0, 1) < 0) {
			(void)munmap((void *)schedmix_sem, sizeof(*schedmix_sem));
			schedmix_sem = NULL;
		}
	} else {
		schedmix_sem = NULL;
	}
#endif

	(void)stress_get_setting("schedmix-procs", &schedmix_procs);

	for (i = 0; i < schedmix_procs; i++)
		pids[i] = -1;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);


	for (i = 0; i < schedmix_procs; i++) {
		stress_mwc_reseed();

		pids[i] = fork();
		if (pids[i] < 0) {
			continue;
		} else if (pids[i] == 0) {
			VOID_RET(int, nice(stress_mwc8modn(7)));
			stress_parent_died_alarm();
			(void)stress_change_cpu(args, parent_cpu);
			_exit(stress_schedmix_child(args));
		}
	}

	do {
		pause();
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_SCHEDMIX_SEM)
	if (schedmix_sem) {
		(void)sem_destroy(&schedmix_sem->sem);
		(void)munmap((void *)schedmix_sem, sizeof(*schedmix_sem));
	}
#endif

	return stress_kill_and_wait_many(args, pids, schedmix_procs, SIGALRM, true);
}

stressor_info_t stress_schedmix_info = {
	.stressor = stress_schedmix,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_schedmix_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
