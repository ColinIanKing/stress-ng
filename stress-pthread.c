/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-pthread.h"

#if defined(HAVE_MODIFY_LDT)
#include <asm/ldt.h>
#endif

#if defined(HAVE_LINUX_FUTEX_H)
#include <linux/futex.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#define MIN_PTHREAD		(1)
#define MAX_PTHREAD		(30000)
#define DEFAULT_PTHREAD		(1024)

#if defined(__NR_get_thread_area)
#define HAVE_GET_THREAD_AREA
#endif

#if defined(__NR_get_robust_list)
#define HAVE_GET_ROBUST_LIST
#endif

#if defined(__NR_set_robust_list)
#define HAVE_SET_ROBUST_LIST
#endif

static const stress_help_t help[] = {
	{ NULL,	"pthread N",	 "start N workers that create multiple threads" },
	{ NULL,	"pthread-max P", "create P threads at a time by each worker" },
	{ NULL,	"pthread-ops N", "stop pthread workers after N bogo threads created" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_LIB_PTHREAD)

/* per pthread data */
typedef struct {
	pthread_t pthread;		/* The pthread */
	int	  ret;			/* pthread create return */
	uint64_t  index;		/* which pthread */
	volatile double	t_create;	/* time when created */
	volatile double	t_run;		/* time when thread started running */
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
	void 	 *stack;		/* pthread stack */
#endif
} stress_pthread_info_t;

static pthread_cond_t cond;
static pthread_mutex_t mutex;
static shim_pthread_spinlock_t spinlock;
static volatile bool keep_thread_running_flag;
static volatile bool keep_running_flag;
static uint64_t pthread_count;
static stress_pthread_info_t pthreads[MAX_PTHREAD];

#endif

static const stress_opt_t opts[] = {
	{ OPT_pthread_max, "pthread-max", TYPE_ID_UINT64, MIN_PTHREAD, MAX_PTHREAD, NULL },
	END_OPT,
};

#if defined(HAVE_LIB_PTHREAD)

#define DEFAULT_STACK_MIN		(8 * KB)

#if defined(HAVE_GET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_get_robust_list)
static inline long int sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr)
{
	return syscall(__NR_get_robust_list, pid, head_ptr, len_ptr);
}
#endif

#if defined(HAVE_SET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_set_robust_list)
static inline long int sys_set_robust_list(struct robust_list_head *head, size_t len)
{
	return syscall(__NR_set_robust_list, head, len);
}
#endif

static inline void stop_running(void)
{
	keep_running_flag = false;
	keep_thread_running_flag = false;
	stress_continue_set_flag(false);
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
 *  stress_pthread_tid_address()
 *	exercise tid address kernel interfaces. Fetch tid addr
 *	if prctl() allows and set this back using set_tid_address.
 *	Sanity check the current pthread tid is the same as the
 *	one returned by set_tid_address.
 */
static void OPTIMIZE3 stress_pthread_tid_address(stress_args_t *args)
{
#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL) &&		\
    defined(PR_GET_TID_ADDRESS) &&	\
    defined(__NR_set_tid_address) &&	\
    defined(HAVE_KERNEL_ULONG_T) &&	\
    defined(HAVE_SYSCALL)
	/*
	 *   prctl(2) states:
	 *    "Note that since the prctl() system call does not have a compat
	 *     implementation for the AMD64 x32 and MIPS n32 ABIs, and
	 *     the kernel writes out a pointer using the kernel's pointer
	 *     size, this operation expects a user-space buffer of 8 (not
	 *     4) bytes on these ABIs."
	 *
	 *   Use 64 bit tid_addr as default.
	 */
	uint64_t tid_addr = 0;

	if (LIKELY(prctl(PR_GET_TID_ADDRESS, &tid_addr, 0, 0, 0) == 0)) {
		unsigned long int set_tid_addr;

		if (sizeof(void *) == 4)  {
			set_tid_addr = stress_little_endian() ?
				(tid_addr & 0xffffffff) : (tid_addr >> 32);
		} else {
			set_tid_addr = (unsigned long int)tid_addr;
		}

		if (set_tid_addr) {
			pid_t tid1, tid2;

			/* Nullify */
			VOID_RET(pid_t, (pid_t)syscall(__NR_set_tid_address, NULL));

			/* This always succeeds */
			tid1 = (pid_t)syscall(__NR_set_tid_address, set_tid_addr);

			errno = 0;
			tid2 = shim_gettid();
			if (UNLIKELY((errno == 0) && (tid1 != tid2))) {
				pr_fail("%s: set_tid_address failed, returned tid %d, expecting tid %d\n",
					args->name, (int)tid1, (int)tid2);
			}
		}
	}
#else
	(void)args;
#endif
}

/*
 *  stress_pthread_func()
 *	pthread specific system call stressor
 */
static void *stress_pthread_func(void *parg)
{
	int ret;
	const double t_run = stress_time_now();
	pid_t tgid_unused;
	const pid_t tgid = getpid();
#if defined(HAVE_GETTID)
	const pid_t tid = shim_gettid();
#else
	const pid_t tid = 0;
#endif
#if defined(HAVE_GET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
	struct robust_list_head *head;
	size_t len;
#endif
	const stress_pthread_args_t *spa = (stress_pthread_args_t *)parg;
	stress_args_t *args = spa->args;
	stress_pthread_info_t *pthread_info = (stress_pthread_info_t *)spa->data;

	pthread_info->t_run = t_run;
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_GET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
	/*
	 *  Check that get_robust_list() works OK
	 */
	if (UNLIKELY(sys_get_robust_list(0, &head, &len) < 0)) {
		if (errno != ENOSYS) {
			pr_fail("%s: get_robust_list failed, tid=%d, errno=%d (%s)\n",
				args->name, (int)tid, errno, strerror(errno));
			goto die;
		}
	} else {
#if defined(HAVE_SET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
		if ((head != NULL) && (len > 0)) {
			struct robust_list_head new_head = *head;

			/* Currently disabled, valgrind complains that head is out of range */
			if (sys_set_robust_list(&new_head, len) < 0) {
				if (UNLIKELY(errno != ENOSYS)) {
					pr_fail("%s: set_robust_list failed, tid=%d, errno=%d (%s)\n",
						args->name, (int)tid, errno, strerror(errno));
						goto die;
				}
			}

			/* Exercise invalid zero length */
			VOID_RET(long int, sys_set_robust_list(&new_head, 0));

#if 0
			/* Exercise invalid length */
			VOID_RET(long int, sys_set_robust_list(new_head, (size_t)-1));
#endif
		}
#endif
	/*
	 *  Check get_robust_list with an invalid PID
	 */
	}
	VOID_RET(long int, sys_get_robust_list(-1, &head, &len));
#endif

	/*
	 *  Exercise tgkill (no-op on systems that do
	 *  not support this system call)
	 */
	VOID_RET(int, shim_tgkill(tgid, tid, 0));
	VOID_RET(int, shim_tgkill(-1, tid, 0));
	VOID_RET(int, shim_tgkill(tgid, -1, 0));
	VOID_RET(int, shim_tgkill(tgid, tid, -1));

	/*
	 *  Exercise tkill, this is either supported directly, emulated
	 *  by tgkill or a no-op if systems don't support tkill or tgkill
	 */
	VOID_RET(int, shim_tkill(tid, 0));
	VOID_RET(int, shim_tkill(-1, 0));
	VOID_RET(int, shim_tkill(tid, -1));

	/*
	 *  Exercise with invalid tgid
	 */
	tgid_unused = stress_get_unused_pid_racy(false);
	VOID_RET(int, shim_tgkill(tgid_unused, tid, 0));
	VOID_RET(int, shim_tgkill(tgid, tgid_unused, 0));
	VOID_RET(int, shim_tkill(tgid_unused, 0));

#if defined(HAVE_ASM_LDT_H) && 		\
    defined(STRESS_ARCH_X86) &&		\
    defined(HAVE_USER_DESC) && 		\
    defined(HAVE_GET_THREAD_AREA) &&	\
    defined(HAVE_SYSCALL) &&		\
    defined(__NR_get_thread_area) &&	\
    defined(__NR_set_thread_area)
	{
		struct user_desc u_info;

		/*
		 *  Exercise get_thread_area only for x86
		 */
		ret = (int)syscall(__NR_get_thread_area, &u_info);
#if defined(HAVE_GET_THREAD_AREA)
		if (ret == 0) {
			VOID_RET(int, (int)syscall(__NR_set_thread_area, &u_info));
		}
#else
		(void)ret;
#endif
	}
#endif

	/*
	 *  Bump count of running threads
	 */
	ret = shim_pthread_spin_lock(&spinlock);
	if (UNLIKELY(ret)) {
		pr_fail("%s: pthread_spin_lock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}
	pthread_count++;
	ret = shim_pthread_spin_unlock(&spinlock);
	if (UNLIKELY(ret)) {
		pr_fail("%s: pthread_spin_unlock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}

	/*
	 *  Did parent inform pthreads to terminate?
	 */
	if (UNLIKELY(!keep_thread_running()))
		goto die;

	/*
	 *  Wait for controlling thread to
	 *  indicate it is time to die
	 */
	ret = pthread_mutex_lock(&mutex);
	if (UNLIKELY(ret)) {
		pr_fail("%s: pthread_mutex_lock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}
	while (keep_thread_running()) {
#if defined(CLOCK_MONOTONIC)
		struct timespec abstime;

		if (clock_gettime(CLOCK_MONOTONIC, &abstime) < 0)
			goto yield;
		abstime.tv_nsec += 10000000;
		if (abstime.tv_nsec >= STRESS_NANOSECOND) {
			abstime.tv_nsec -= STRESS_NANOSECOND;
			abstime.tv_sec++;
		}

		ret = pthread_cond_timedwait(&cond, &mutex, &abstime);
		if (ret) {
			if (UNLIKELY(ret != ETIMEDOUT)) {
				pr_fail("%s: pthread_cond_wait failed, tid=%d, errno=%d (%s)\n",
					args->name, (int)tid, ret, strerror(ret));
				break;
			}
		}
yield:
#endif
		(void)shim_sched_yield();
	}
	ret = pthread_mutex_unlock(&mutex);
	if (UNLIKELY(ret))
		pr_fail("%s: pthread_mutex_unlock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));

#if defined(HAVE_SETNS)
	{
		int fd;

		fd = open("/proc/self/ns/uts", O_RDONLY);
		if (fd >= 0) {
			/*
			 *  Capabilities have been dropped
			 *  so this will always fail, but
			 *  lets exercise it anyhow.
			 */
			(void)setns(fd, 0);
			(void)close(fd);
		}
	}
#endif

#if defined(HAVE_PTHREAD_SIGQUEUE) &&	\
    defined(HAVE_SIGWAITINFO)
	{
		siginfo_t info;
		sigset_t mask;
		struct timespec timeout;

		(void)shim_memset(&info, 0, sizeof(info));
		(void)sigemptyset(&mask);
		(void)sigaddset(&mask, SIGUSR1);

		timeout.tv_sec = 0;
		timeout.tv_nsec = 1000000;

		/* Ignore error return, just exercise the call */
		VOID_RET(int, sigtimedwait(&mask, &info, &timeout));
	}
#endif

die:
	(void)keep_running();

	stress_pthread_tid_address(args);

	return &g_nowt;
}

/*
 *  stress_pthread()
 *	stress by creating pthreads
 */
static int stress_pthread(stress_args_t *args)
{
	char msg[64];
	bool locked = false;
	uint64_t limited = 0, attempted = 0, maximum = 0;
	uint64_t pthread_max = DEFAULT_PTHREAD;
	int ret;
	stress_pthread_args_t pargs = { args, NULL, 0 };
	sigset_t set;
	double count = 0.0, duration = 0.0, average;
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
	const size_t stack_size = STRESS_MAXIMUM(DEFAULT_STACK_MIN, stress_get_min_pthread_stack_size());
#endif
#if defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL)
	pthread_mutexattr_t mutex_attr;
	bool mutex_attr_init;
#endif

	keep_running_flag = true;

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	(void)sigemptyset(&set);
	(void)sigaddset(&set, SIGALRM);
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	if (!stress_get_setting("pthread-max", &pthread_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pthread_max = MAX_PTHREAD;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pthread_max = MIN_PTHREAD;
	}

	ret = pthread_cond_init(&cond, NULL);
	if (ret) {
		pr_fail("%s: pthread_cond_init failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}
	ret = shim_pthread_spin_init(&spinlock, SHIM_PTHREAD_PROCESS_SHARED);
	if (ret) {
		pr_fail("%s: pthread_spin_init failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}
#if defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL)
	/*
	 *  Attempt to use priority inheritance on mutex
	 */
	ret = pthread_mutexattr_init(&mutex_attr);
	if (ret == 0) {
		mutex_attr_init = true;
		VOID_RET(int, pthread_mutexattr_setprotocol(&mutex_attr, PTHREAD_PRIO_INHERIT));
		VOID_RET(int, pthread_mutexattr_setprioceiling(&mutex_attr, 127));
	} else {
		mutex_attr_init = false;
	}
#endif

	ret = pthread_mutex_init(&mutex, NULL);
	if (ret) {
		pr_fail("%s: pthread_mutex_init failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t i, j;

		keep_thread_running_flag = true;
		pthread_count = 0;

		(void)shim_memset(&pthreads, 0, sizeof(pthreads));
		ret = pthread_mutex_lock(&mutex);
		if (UNLIKELY(ret)) {
			stop_running();
			continue;
		}
		for (i = 0; i < pthread_max; i++) {
			pthreads[i].ret = -1;
			pthreads[i].index = i;
		}

		for (i = 0; i < pthread_max; i++) {
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			pthread_attr_t attr;
#endif
			pargs.data = (void *)&pthreads[i];

#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			/*
			 *  We allocate our own per pthread stack to ensure we
			 *  have one available before the pthread is started
			 *  and this allows us to exercise the pthread stack
			 *  setting.
			 */
			pthreads[i].stack = (void *)calloc(1, stack_size);
			if (UNLIKELY(!pthreads[i].stack))
				break;

			ret = pthread_attr_init(&attr);
			if (UNLIKELY(ret)) {
				(void)pthread_mutex_unlock(&mutex);
				pr_fail("%s: pthread_attr_init failed, errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
				goto reap;
			}
			ret = pthread_attr_setstack(&attr, pthreads[i].stack, stack_size);
			if (UNLIKELY(ret)) {
				(void)pthread_mutex_unlock(&mutex);
				pr_fail("%s: pthread_attr_setstack failed, errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
				goto reap;
			}
#endif

			pthreads[i].t_create = stress_time_now();
			pthreads[i].t_run = pthreads[i].t_create;
			pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
				stress_pthread_func, (void *)&pargs);
			if (UNLIKELY(pthreads[i].ret)) {
				/* Out of resources, don't try any more */
				if (pthreads[i].ret == EAGAIN) {
					limited++;
					break;
				}
				/* Something really unexpected */
				(void)pthread_mutex_unlock(&mutex);
				pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
					args->name, pthreads[i].ret, strerror(pthreads[i].ret));
				stop_running();
				goto reap;
			}
			if (i + 1 > maximum)
				maximum = i + 1;
			stress_bogo_inc(args);
			if (UNLIKELY(!(keep_running() && stress_continue(args))))
				break;
		}
		attempted++;
		ret = pthread_mutex_unlock(&mutex);
		if (UNLIKELY(ret)) {
			stop_running();
			goto reap;

		}
		/*
		 *  Wait until they are all started or
		 *  we get bored waiting..
		 */
		for (j = 0; j < 1000; j++) {
			bool all_running = false;

			if (UNLIKELY(!stress_continue(args))) {
				stop_running();
				goto reap;
			}

			if (!locked) {
				ret = shim_pthread_spin_lock(&spinlock);
				if (UNLIKELY(ret)) {
					pr_fail("%s: pthread_spin_lock failed (parent), errno=%d (%s)\n",
						args->name, ret, strerror(ret));
					stop_running();
					goto reap;
				}
				locked = true;
			}
			all_running = (pthread_count == i);

			if (locked) {
				ret = shim_pthread_spin_unlock(&spinlock);
				if (UNLIKELY(ret)) {
					pr_fail("%s: pthread_spin_unlock failed (parent), errno=%d (%s)\n",
						args->name, ret, strerror(ret));
					stop_running();
					/* We failed to unlock, so don't try again on reap */
					goto reap;
				}
				locked = false;
			}

			if (all_running)
				break;
		}

#if defined(HAVE_PTHREAD_SIGQUEUE) &&	\
    defined(HAVE_SIGWAITINFO)
		for (j = 0; j < i; j++) {
			union sigval value;

			if (pthreads[j].ret)
				continue;

			(void)shim_memset(&value, 0, sizeof(value));
			VOID_RET(int, pthread_sigqueue(pthreads[j].pthread, SIGUSR1, value));
		}
#endif

reap:
		keep_thread_running_flag = false;
		ret = pthread_cond_broadcast(&cond);
		if (UNLIKELY(ret)) {
			pr_fail("%s: pthread_cond_broadcast failed (parent), errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			stop_running();
			/* fall through and unlock */
		}
		for (j = 0; j < i; j++) {
			if (pthreads[j].ret == 0) {
				double t;

				ret = pthread_join(pthreads[j].pthread, NULL);
				if (UNLIKELY((ret) && (ret != ESRCH))) {
					pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
						args->name, ret, strerror(ret));
					stop_running();
				}

				t = pthreads[j].t_run - pthreads[j].t_create;
				if (t > 0.0) {
					duration += t;
					count += 1;
				}
			}
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			if (pthreads[j].stack != NULL)
				free(pthreads[j].stack);
#endif
		}
	} while (!locked && keep_running() && stress_continue(args));

	average = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs to start a pthread",
		average * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
	(void)snprintf(msg, sizeof(msg), "%% of %" PRIu64 " pthreads created",
		pthread_max * args->instances);
	if (attempted > 0)
		stress_metrics_set(args, 1, msg,
			100.0 * (double)(attempted - limited) / (double)attempted,
			STRESS_METRIC_GEOMETRIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(HAVE_PTHREAD_MUTEXATTR_T) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_INIT) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_DESTROY) &&		\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPRIOCEILING) &&	\
    defined(HAVE_PTHREAD_MUTEXATTR_SETPROTOCOL)
	if (mutex_attr_init)
		(void)pthread_mutexattr_destroy(&mutex_attr);
#endif
	(void)pthread_cond_destroy(&cond);
	(void)pthread_mutex_destroy(&mutex);
	(void)shim_pthread_spin_destroy(&spinlock);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_pthread_info = {
	.stressor = stress_pthread,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_pthread_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
