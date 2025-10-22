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
#include "core-asm-generic.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

#include <sched.h>
#include <time.h>
#include <sys/times.h>

#if defined(__NR_set_mempolicy)
#define HAVE_SET_MEMPOLICY
#endif
#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    defined(SIGRTMIN) &&		\
    defined(SIGEV_SIGNAL) &&		\
    defined(CLOCK_REALTIME)
#define HAVE_TIMER_CLOCK_REALTIME
#define TIMER_NS	(250000000)
#endif

#if defined(HAVE_SCHED_SETAFFINITY) &&		\
    (defined(_POSIX_PRIORITY_SCHEDULING) ||	\
     defined(__linux__)) &&			\
    (defined(SCHED_BATCH) ||			\
     defined(SCHED_DEADLINE) ||			\
     defined(SCHED_EXT) ||			\
     defined(SCHED_IDLE) ||			\
     defined(SCHED_FIFO) ||			\
     defined(SCHED_OTHER) ||			\
     defined(SCHED_RR) ||			\
     defined(SCHED_EXT)) && 			\
    !defined(__OpenBSD__) &&			\
    !defined(__minix__) &&			\
    !defined(__APPLE__)
#define HAVE_SCHEDULING
#endif

#define MAX_CPU_SCHED_PROCS		(16)

static const stress_help_t help[] = {
	{ NULL,	"cpu-sched N",		"start N workers that exercise cpu scheduling" },
	{ NULL,	"cpu-sched-ops N",	"stop after N bogo cpu scheduling operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SCHEDULING) &&		\
    defined(HAVE_SCHED_SETSCHEDULER)
static stress_pid_t stress_cpu_sched_pids[MAX_CPU_SCHED_PROCS];
static uint32_t n_cpus;
static uint32_t *cpus;

#if defined(HAVE_TIMER_CLOCK_REALTIME)
static timer_t timerid;
#endif

#if defined(HAVE_SET_MEMPOLICY)
static stress_numa_mask_t *numa_mask = NULL;

static const int mpol_modes[] = {
	0,
#if defined(MPOL_BIND)
	MPOL_BIND,
#if defined(MPOL_F_NUMA_BALANCING)
	MPOL_BIND | MPOL_F_NUMA_BALANCING,
#endif
#endif
#if defined(MPOL_INTERLEAVE)
	MPOL_INTERLEAVE,
#endif
#if defined(MPOL_PREFERRED)
	MPOL_PREFERRED,
#endif
#if defined(MPOL_LOCAL)
	MPOL_LOCAL,
#endif
};
#endif

/*
 *  "Normal" non-realtime scheduling policies
 */
static const int policies[] = {
#if defined(SCHED_OTHER)
	SCHED_OTHER,
#endif
#if defined(SCHED_OTHER) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_OTHER | SCHED_RESET_ON_FORK,
#endif
#if defined(SCHED_BATCH)
	SCHED_BATCH,
#endif
#if defined(SCHED_BATCH) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_BATCH | SCHED_RESET_ON_FORK,
#endif
#if defined(SCHED_EXT)
	SCHED_EXT,
#endif
#if defined(SCHED_EXT) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_EXT | SCHED_RESET_ON_FORK,
#endif
#if defined(SCHED_IDLE)
	SCHED_IDLE,
#endif
#if defined(SCHED_IDLE) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_IDLE | SCHED_RESET_ON_FORK,
#endif
#if defined(SCHED_DEADLINE) &&	\
    defined(HAVE_SCHED_GETATTR)
	SCHED_DEADLINE,
#endif
#if defined(SCHED_FIFO)
	SCHED_FIFO,
#endif
#if defined(SCHED_FIFO) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_FIFO | SCHED_RESET_ON_FORK,
#endif
#if defined(SCHED_RR)
	SCHED_RR,
#endif
#if defined(SCHED_RR) &&	\
    defined(SCHED_RESET_ON_FORK)
	SCHED_RR | SCHED_RESET_ON_FORK,
#endif
};

/*
 *  stress_cpu_sched_rand_cpu_idx()
 *	return a random cpu index for cpus[] array
 */
static int stress_cpu_sched_rand_cpu_idx(void)
{
	return (n_cpus > 0) ? (int)stress_mwc32modn(n_cpus) : 0;
}

/*
 *  stress_cpu_sched_nice()
 *	attempt to try to use autogroup for linux, this
 *	may fail with EAGAIN if not privileged or has been
 *	adjusted too many times.
 */
static int stress_cpu_sched_nice(const int inc)
{
#if defined(__linux__) &&		\
    defined(HAVE_GETPRIORITY) &&	\
    defined(HAVE_SETPRIORITY) && 	\
    defined(PRIO_PROCESS)
	int prio, ret, saved_errno;
	char buffer[32];

	errno = 0;
	/* getpriority can return -1, so check errno */
	prio = getpriority(PRIO_PROCESS, 0) + inc;
	prio = (prio > 19) ? 19 : prio;

	/* failed? fall back to shim'd variant of nice */
	if (UNLIKELY(errno != 0))
		return shim_nice(inc);
	ret = setpriority(PRIO_PROCESS, 0, prio);
	saved_errno = errno;
	(void)snprintf(buffer, sizeof(buffer), "%d\n", prio);
	VOID_RET(ssize_t, stress_system_write("/proc/self/autogroup", buffer, strlen(buffer)));

	errno = saved_errno;
	return ret;
#else
	return shim_nice(inc);
#endif
}

/*
 *  stress_cpu_sched_setaffinity()
 *	attempt to set CPU affinity of process 'pid' to cpu 'cpu'
 */
static int stress_cpu_sched_setaffinity(const pid_t pid, const int cpu)
{
	cpu_set_t cpu_set;
	int ret;

	CPU_ZERO(&cpu_set);
	CPU_SET(cpu, &cpu_set);
	ret = sched_setaffinity(pid, sizeof(cpu_set), &cpu_set);
	if (ret == 0) {
		CPU_ZERO(&cpu_set);
		(void)sched_getaffinity(pid, sizeof(cpu_set), &cpu_set);
	}
	return 0;
}

/*
 *  stress_cpu_sched_setscheduler()
 *	attempt to set CPU scheduler of process 'pid to random scheduler
 */
static int stress_cpu_sched_setscheduler(const pid_t pid)
{
	struct sched_param param;
	const uint32_t i = stress_mwc8modn((uint8_t)SIZEOF_ARRAY(policies));
	int ret, policy_masked, policy, prio;
#if defined(SCHED_FIFO) ||	\
     defined(SCHED_RR)
	int prio_min, prio_max, prio_range;
#endif
#if defined(SCHED_DEADLINE) &&	\
    defined(HAVE_SCHED_GETATTR)
	struct shim_sched_attr attr;
	uint64_t rndtime;
#endif

	(void)shim_memset(&param, 0, sizeof(param));
	policy = policies[i];
#if defined(SCHED_RESET_ON_FORK)
	policy_masked = policy & ~SCHED_RESET_ON_FORK;
#else
	policy_masked = policy;
#endif

	switch (policy_masked) {
#if defined(SCHED_FIFO)
	case SCHED_FIFO:
#endif
#if defined(SCHED_RR)
	case SCHED_RR:
#endif
#if defined(SCHED_FIFO) ||	\
    defined(SCHED_RR)
		prio_min = sched_get_priority_min(policy_masked);
		prio_max = sched_get_priority_max(policy_masked);
		prio_range = (prio_max - prio_min) / 2;
		prio = prio_max - (int)stress_mwc32modn(prio_range);
		break;
#endif
#if defined(SCHED_DEADLINE) &&	\
    defined(HAVE_SCHED_GETATTR)
	case SCHED_DEADLINE:
#endif
	default:
		prio = 0;
		break;
	}

	switch (policy_masked) {
#if defined(SCHED_DEADLINE) &&	\
    defined(HAVE_SCHED_GETATTR)
	case SCHED_DEADLINE:
		rndtime = (uint64_t)stress_mwc8modn(64) + 32;

		(void)shim_memset(&attr, 0, sizeof(attr));
		attr.size = sizeof(attr);
#if defined(SCHED_FLAG_RECLAIM)
		attr.sched_flags = stress_mwc1() ? 0 : SCHED_FLAG_RECLAIM;
#else
		attr.sched_flags = 0;
#endif
		attr.sched_nice = 0;
		attr.sched_priority = prio;
		attr.sched_policy = policy;
		/* runtime <= deadline <= period */
		attr.sched_runtime = rndtime * 100000;
		attr.sched_deadline = rndtime * 2000000;
		attr.sched_period = rndtime * 4000000;
		(void)shim_sched_setattr(0, &attr, 0);
		break;
#endif
	default:
		param.sched_priority = prio;
		ret = sched_setscheduler(pid, policy, &param);
#if defined(SCHED_RESET_ON_FORK)
		if ((ret != 0) && (policy & SCHED_RESET_ON_FORK)) {
			ret = sched_setscheduler(pid, policy_masked, &param);
		}
#endif
		if (ret == 0)
			(void)sched_getscheduler(pid);
		break;
	}
	return 0;
}

/*
 *  stress_cpu_sched_mix_pids()
 *	change order of pids
 */
static void stress_cpu_sched_mix_pids(stress_pid_t *mix_pids, stress_pid_t *orig_pids, const size_t n)
{
	register int i;
	register size_t j;

	switch (stress_mwc8modn(3)) {
	case 0:
		/* In order */
		(void)memcpy(mix_pids, orig_pids, n * sizeof(*mix_pids));
		break;
	case 1:
		/* Shuffle */
		(void)memcpy(mix_pids, orig_pids, n * sizeof(*mix_pids));
		for (i = 0; i < 3; i++) {
			for (j = 0; j < n; j++) {
				stress_pid_t tmp;

				size_t k = stress_mwc8modn(n);
				tmp = mix_pids[j];
				mix_pids[j] = mix_pids[k];
				mix_pids[k] = tmp;
			}
		}
		break;
	case 2:
		/* Reverse order */
		for (j = 0; j < n; j++)
			mix_pids[j] = orig_pids[(n - 1) - j];
		break;
	}
}

#if defined(HAVE_TIMER_CLOCK_REALTIME)

/*
 *  stress_cpu_sched_hrtimer_sigprocmask()
 *	block/unblock SIGRTMIN
 */
static int stress_cpu_sched_hrtimer_sigprocmask(const int how)
{
	sigset_t sigset;

	sigemptyset(&sigset);
	sigaddset(&sigset, SIGRTMIN);
	return sigprocmask(how, &sigset, NULL);
}

/*
 *  stress_cpu_sched_hrtimer_set()
 *	set hrtimer to fire every nsec nanosecs
 */
static void stress_cpu_sched_hrtimer_set(const long int nsec)
{
	if (LIKELY(timerid != (timer_t)-1)) {
		struct itimerspec timer;

		timer.it_value.tv_nsec = nsec;
		timer.it_value.tv_sec = 0;
		timer.it_interval.tv_nsec = nsec;
		timer.it_interval.tv_sec = 0;
		(void)timer_settime(timerid, 0, &timer, NULL);
#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_TIMERSLACK)
		(void)prctl(PR_SET_TIMERSLACK, stress_mwc16() * 10);
#endif
	}
}

/*
 *  stress_cpu_sched_hrtimer_handler
 *	handle hrtimer signal, resched and set next timer
 */
static void MLOCKED_TEXT stress_cpu_sched_hrtimer_handler(int sig)
{
	sigset_t sigset;
	bool cancel_timer = false;

	(void)sig;

	sigemptyset(&sigset);
	if (sigpending(&sigset) < 0) {
		cancel_timer = true;
	} else if (sigismember(&sigset, SIGALRM)) {
		cancel_timer = true;
	}
	if (cancel_timer) {
		stress_cpu_sched_hrtimer_sigprocmask(SIG_BLOCK);
		stress_cpu_sched_hrtimer_set(0);
		return;
	}

	if (LIKELY(stress_continue_flag())) {
		const pid_t pid = getpid();
		if (n_cpus > 0) {
			const int cpu_idx = stress_cpu_sched_rand_cpu_idx();

			(void)stress_cpu_sched_setaffinity(pid, (int)cpus[cpu_idx]);
			(void)stress_cpu_sched_setscheduler(pid);
		}
		stress_cpu_sched_hrtimer_set(TIMER_NS);
	}
}
#endif

/*
 *  stress_cpu_sched_set_handler()
 *	set up HR timer handler
 */
static void stress_cpu_sched_set_handler(void)
{
#if defined(HAVE_TIMER_CLOCK_REALTIME)
	struct sigaction action;

	timerid = (timer_t)-1;

	(void)shim_memset(&action, 0, sizeof(action));
	action.sa_handler = stress_cpu_sched_hrtimer_handler;
	(void)sigemptyset(&action.sa_mask);
	if (LIKELY(sigaction(SIGRTMIN, &action, NULL) == 0)) {
		struct sigevent sev;
		int timer_ret = -1;

		(void)shim_memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &timerid;
		timer_ret = timer_create(CLOCK_REALTIME, &sev, &timerid);
		if (LIKELY(timer_ret == 0)) {
			const uint64_t ns = stress_mwc64modn(TIMER_NS >> 1) + (TIMER_NS >> 1);

			stress_cpu_sched_hrtimer_set(ns);
		} else {
			timerid = (timer_t)-1;
		}
	}
#endif
}

/*
 *  stress_cpu_sched_child_exercise()
 *	exercise scheduler
 */
static void stress_cpu_sched_child_exercise(const pid_t pid, const int cpu)
{
	unsigned int new_cpu, node;

	(void)stress_cpu_sched_setaffinity(pid, cpu);
	(void)shim_getcpu(&new_cpu, &node, NULL);
	(void)shim_usleep_interruptible(0);
	(void)stress_cpu_sched_setscheduler(pid);
	(void)shim_sched_yield();
}

/*
 *  stress_cpu_sched_fork()
 *	create a process and make it exercise scheduling
 */
static void stress_cpu_sched_fork(stress_args_t *args)
{
	pid_t pid;
	int retry = 0;

	stress_cpu_sched_set_handler();

#if defined(HAVE_TIMER_CLOCK_REALTIME)
	stress_cpu_sched_hrtimer_set(0);
	if (stress_cpu_sched_hrtimer_sigprocmask(SIG_BLOCK) < 0)
		return;
#endif
again:
	pid = fork();
	if (pid == -1) {
                if ((retry++ < 10) && stress_redo_fork(args, errno)) {
			(void)shim_usleep_interruptible(50000);
                        goto again;
		}
		goto err;
	} else if (pid == 0) {
		const pid_t child_pid = getpid();
		int cpu_idx;

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
#if defined(HAVE_TIMER_CLOCK_REALTIME)
		if (timerid != (timer_t)-1) {
			(void)timer_delete(timerid);
			timerid = (timer_t)-1;
		}
#endif
		for (cpu_idx = 0; cpu_idx < (int)n_cpus; cpu_idx++) {
			stress_cpu_sched_child_exercise(child_pid, cpus[cpu_idx]);
		}
		(void)stress_cpu_sched_nice(1);
		for (cpu_idx = 0; cpu_idx < (int)n_cpus; cpu_idx++) {
			stress_cpu_sched_child_exercise(child_pid, cpus[(n_cpus - 1) - cpu_idx]);
		}
		(void)stress_cpu_sched_nice(1);
		for (cpu_idx = 0; cpu_idx < (int)n_cpus; cpu_idx++) {
			const int cpu = cpus[stress_cpu_sched_rand_cpu_idx()];

			stress_cpu_sched_child_exercise(child_pid, cpu);
		}
		(void)stress_cpu_sched_nice(1);
		(void)shim_sched_yield();
		_exit(0);
	} else {
		int status;

		if (shim_waitpid(pid, &status, 0) < 0) {
			/* apply hammer */
			(void)stress_kill_pid_wait(pid, &status);
		}
	}
err:
#if defined(HAVE_TIMER_CLOCK_REALTIME)
	stress_cpu_sched_hrtimer_set(TIMER_NS);
	(void)stress_cpu_sched_hrtimer_sigprocmask(SIG_UNBLOCK);
#else
	stress_asm_nothing();
#endif
}

/*
 *  stress_cpu_sched_next_cpu_idx()
 *	select next cpu index
 */
static int stress_cpu_sched_next_cpu_idx(const int instance, const int last_cpu_idx)
{
	struct timeval now;
	int cpu_idx;

	if (gettimeofday(&now, NULL) < 0)
		return stress_cpu_sched_rand_cpu_idx();

	switch (now.tv_sec % 12) {
	default:
	case 0:
		/* random selection */
		return stress_cpu_sched_rand_cpu_idx();
	case 1:
		/* next cpu index */
		cpu_idx = last_cpu_idx + 1;
		return cpu_idx >= (int)n_cpus ? 0 : cpu_idx;
	case 2:
		/* prev cpu */
		cpu_idx = last_cpu_idx - 1;
		return cpu_idx < 0 ? (int)n_cpus - 1 : cpu_idx;
	case 3:
		/* based on seconds past EPOCH */
		return (int)(now.tv_sec % (int)n_cpus);
	case 4:
		/* instance and seconds past EPOCH */
		return (instance + (now.tv_sec / 12)) % n_cpus;
	case 5:
		/* stride n_cpus by instance number */
		return (last_cpu_idx + instance + 1) % n_cpus;
	case 6:
		/* based on instance number */
		return (int)(instance % n_cpus);
	case 7:
		/* ping pong from last cpu */
		return ((int)n_cpus - 1) - last_cpu_idx;
	case 8:
		/* based on fraction of second */
		return (int)(now.tv_usec / 72813) % n_cpus;
	case 9:
		/* prev with creeping brown noise */
		cpu_idx = last_cpu_idx + (stress_mwc32modn(5) - 2);
		cpu_idx = (cpu_idx < 0) ? cpu_idx + (int)n_cpus : cpu_idx;
		return (int)(cpu_idx % n_cpus);
	case 10:
		/* odd/even */
		cpu_idx = last_cpu_idx ^ 1;
		return (int)(cpu_idx % n_cpus);
	case 11:
		/* +/- 2 */
		cpu_idx = last_cpu_idx ^ 2;
		return (int)(cpu_idx % n_cpus);
	}
	return last_cpu_idx;
}

/*
 *  stress_cpu_sched_exec()
 *	change affinity and scheduler then exec stress-ng that
 *	immediately exits with the --exec-exit option
 */
static void stress_cpu_sched_exec(stress_args_t *args, char *exec_prog)
{
	pid_t pid;
	int retry = 0;

#if defined(HAVE_TIMER_CLOCK_REALTIME)
	stress_cpu_sched_hrtimer_set(0);
	if (stress_cpu_sched_hrtimer_sigprocmask(SIG_BLOCK) < 0)
		return;
#endif

again:
	pid = fork();
	if (pid < 0) {
                if ((retry++ < 10) && stress_redo_fork(args, errno)) {
			(void)shim_usleep_interruptible(50000);
                        goto again;
		}
#if defined(HAVE_TIMER_CLOCK_REALTIME)
		(void)stress_cpu_sched_hrtimer_sigprocmask(SIG_UNBLOCK);
#endif
		return;
	} else if (pid == 0) {
		char *argv[4], *env[2];
		const int cpu_idx = stress_cpu_sched_rand_cpu_idx();
		const pid_t mypid = getpid();

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
#if defined(HAVE_TIMER_CLOCK_REALTIME)
		if (timerid != (timer_t)-1) {
			(void)timer_delete(timerid);
			timerid = (timer_t)-1;
		}
#endif

		if (n_cpus > 0)
			(void)stress_cpu_sched_setaffinity(mypid, cpus[cpu_idx]);
		(void)stress_cpu_sched_setscheduler(mypid);

		argv[0] = exec_prog;
		argv[1] = "--exec-exit";
		argv[2] = NULL;
		argv[3] = NULL;

		env[0] = NULL;
		env[1] = NULL;

		_exit(execve(exec_prog, argv, env));
	} else {
		int status;

		if (shim_waitpid(pid, &status, 0) < 0) {
			/* apply hammer */
			(void)stress_kill_pid_wait(pid, &status);
		}
#if defined(HAVE_TIMER_CLOCK_REALTIME)
		stress_cpu_sched_hrtimer_set(TIMER_NS);
		(void)stress_cpu_sched_hrtimer_sigprocmask(SIG_UNBLOCK);
#endif
	}
}

static int stress_cpu_sched_child(stress_args_t *args, void *context)
{
	/* Child */
	int cpu_idx = 0, rc = EXIT_SUCCESS;
	const int instance = (int)args->instance;
	size_t i;
	stress_pid_t pids[MAX_CPU_SCHED_PROCS];
	char exec_path[PATH_MAX];
	char *exec_prog = stress_get_proc_self_exe(exec_path, sizeof(exec_path));
	const bool cap_sys_nice = stress_check_capability(SHIM_CAP_SYS_NICE);
	const bool not_root = !stress_check_capability(SHIM_CAP_IS_ROOT);
	uint32_t counter = 0;
	double time_end = stress_time_now() + (double)g_opt_timeout;

	(void)context;

#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_TIMERSLACK)
	(void)prctl(PR_SET_TIMERSLACK, 5);
#endif

	(void)shim_memset(pids, 0, sizeof(pids));
	for (i = 0; i < MAX_CPU_SCHED_PROCS; i++) {
		stress_cpu_sched_pids[i].pid = -1;
	}
	for (i = 0; LIKELY((i < MAX_CPU_SCHED_PROCS) && stress_continue(args)); i++) {
		pid_t pid;
		int retry = 0;

again:
		pid = fork();
		if (pid < 0) {
                	if ((retry++ < 10) && stress_redo_fork(args, errno)) {
				(void)shim_usleep_interruptible(50000);
				goto again;
			}
			stress_cpu_sched_pids[i].pid = -1;
		} else if (pid == 0) {
			pid_t mypid = getpid();
			unsigned int current_cpu, node;
			int n = (int)mypid % 23;
#if defined(HAVE_SET_MEMPOLICY)
			int mode;
#endif

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			stress_parent_died_alarm();

			stress_cpu_sched_set_handler();

			/* pid process re-mix mwc */
			while (n-- > 0)
				stress_mwc32();

			stress_cpu_sched_nice(1 + stress_mwc8modn(8));
			do {
				if (UNLIKELY(stress_time_now() >= time_end))
					break;
				switch (stress_mwc8modn(8)) {
				case 0:
					(void)shim_sched_yield();
					break;
				case 1:
					(void)shim_nanosleep_uint64(stress_mwc32modn(25000));
					break;
				case 2:
					if (cap_sys_nice)
						(void)setpriority(PRIO_PROCESS, mypid, 1 + stress_mwc8modn(18));
					else
						(void)shim_usleep_interruptible(10);
					break;
				case 3:
					(void)shim_usleep_interruptible(0);
					break;
				case 4:
					(void)shim_getcpu(&current_cpu, &node, NULL);
					break;
				case 5:
					for (n = 0; n < 1000; n++)
						stress_asm_nop();
					break;
#if defined(HAVE_SET_MEMPOLICY)
				case 6:
					if (numa_mask) {
						(void)shim_memset((void *)numa_mask->mask, 0, numa_mask->mask_size);
						STRESS_SETBIT(numa_mask->mask, (int)stress_mwc16modn(numa_mask->nodes));
						mode = mpol_modes[stress_mwc8modn(SIZEOF_ARRAY(mpol_modes))];
						(void)shim_set_mempolicy(mode, numa_mask->mask, numa_mask->max_nodes);
					}
#else
					(void)shim_sched_yield();
					(void)shim_sched_yield();
					(void)shim_sched_yield();
					(void)shim_sched_yield();
					(void)shim_sched_yield();
#endif
					break;
				default:
					if (n_cpus > 0) {
						cpu_idx = stress_cpu_sched_next_cpu_idx(instance, cpu_idx);
						(void)stress_cpu_sched_setaffinity(mypid, cpus[cpu_idx]);
					}
					(void)shim_sched_yield();
					(void)sleep(0);
					break;
				}
			} while (stress_continue(args));

#if defined(HAVE_TIMER_CLOCK_REALTIME)
			if (LIKELY(timerid != (timer_t)-1)) {
				(void)timer_delete(timerid);
				timerid = (timer_t)-1;
			}
#endif
			_exit(0);
		} else {
			stress_cpu_sched_pids[i].pid = pid;
		}
	}

	do {
		stress_cpu_sched_mix_pids(pids, stress_cpu_sched_pids, MAX_CPU_SCHED_PROCS);

		for (i = 0; LIKELY((i < MAX_CPU_SCHED_PROCS) && stress_continue(args)); i++) {
			const pid_t pid = pids[i].pid;
			const bool stop_cont = stress_mwc1();

			if (UNLIKELY(pid == -1))
				continue;

			cpu_idx = stress_cpu_sched_next_cpu_idx(instance, cpu_idx);

			if (stop_cont)
				(void)kill(pid, SIGSTOP);
			if (n_cpus > 0) {
				if (UNLIKELY(stress_cpu_sched_setaffinity(pid, cpus[cpu_idx]) < 0)) {
					rc = EXIT_FAILURE;
					break;
				}
			}
			if (UNLIKELY(stress_cpu_sched_setscheduler(pid) < 0)) {
				rc = EXIT_FAILURE;
				break;
			}
#if defined(HAVE_SETPRIORITY) &&	\
    defined(PRIO_PROCESS)
			if (cap_sys_nice)
				(void)setpriority(PRIO_PROCESS, pid, 1 + stress_mwc8modn(18));
#endif
			if (stop_cont)
				(void)kill(pid, SIGCONT);
			stress_bogo_inc(args);
		}
		for (i = 0; (i < LIKELY((MAX_CPU_SCHED_PROCS >> 2)) && stress_continue(args)); i++) {
			const pid_t pid = pids[stress_mwc8modn(MAX_CPU_SCHED_PROCS)].pid;

			if (LIKELY(pid != -1)) {
				(void)kill(pid, SIGSTOP);
				(void)kill(pid, SIGCONT);
			}
		}

		if (n_cpus > 0)
			(void)stress_cpu_sched_setaffinity(args->pid, cpus[stress_cpu_sched_rand_cpu_idx()]);
		(void)shim_sched_yield();

		counter++;
		if (counter & 0x1ff) {
			double min1, min5, min15;
			static bool get_load_avg = true;

			if (get_load_avg) {
				if (stress_get_load_avg(&min1, &min5, &min15) < 0)
					get_load_avg = false;
			}
		}
		if ((counter & 0x01ff) == 0) {
#if defined(HAVE_GETRUSAGE) &&		\
    (defined(RUSAGE_SELF) ||		\
     defined(RUSAGE_CHILDREN) ||	\
     defined(RUSAGE_THREAD))
			struct rusage usage;
#if defined(RUSAGE_SELF)
			(void)shim_getrusage(RUSAGE_SELF, &usage);
#endif
#if defined(RUSAGE_CHILDREN)
			(void)shim_getrusage(RUSAGE_CHILDREN, &usage);
#endif
#if defined(RUSAGE_THREAD)
			(void)shim_getrusage(RUSAGE_CHILDREN, &usage);
#endif
#else
			struct tms t;

			(void)times(&t);
#endif

		}

		if ((counter & 0x03ff) == 0) {
			stress_cpu_sched_fork(args);
#if defined(__linux__)
			stress_system_discard("/sys/kernel/debug/sched/debug");
			stress_system_discard("/proc/pressure/cpu");
			stress_system_discard("/proc/pressure/irq");
			stress_system_discard("/proc/schedstat");
#endif
		}
		if (((counter & 0xfff) == 0) &&
		     exec_prog && not_root) {
			stress_cpu_sched_exec(args, exec_prog);
		}
	} while (stress_continue(args));

	(void)stress_kill_and_wait_many(args, stress_cpu_sched_pids, MAX_CPU_SCHED_PROCS, SIGKILL, false);

	return rc;
}

/*
 *  stress_cpu_sched()
 *	stress by cloning and exiting
 */
static int stress_cpu_sched(stress_args_t *args)
{
	int rc;

	n_cpus = stress_get_usable_cpus(&cpus, true);

#if defined(HAVE_SET_MEMPOLICY)
	numa_mask = stress_numa_mask_alloc();
#endif
	stress_set_oom_adjustment(args, false);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, NULL, stress_cpu_sched_child, 0);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_SET_MEMPOLICY)
	if (numa_mask)
		stress_numa_mask_free(numa_mask);
#endif

	stress_free_usable_cpus(&cpus);
	return rc;
}

const stressor_info_t stress_cpu_sched_info = {
	.stressor = stress_cpu_sched,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_cpu_sched_info = {
        .stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.help = help,
	.verify = VERIFY_ALWAYS,
	.unimplemented_reason = "built without Linux scheduling or sched_setscheduler() system call"
};
#endif
