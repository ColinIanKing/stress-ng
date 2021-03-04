/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"pthread N",	 "start N workers that create multiple threads" },
	{ NULL,	"pthread-ops N", "stop pthread workers after N bogo threads created" },
	{ NULL,	"pthread-max P", "create P threads at a time by each worker" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_LIB_PTHREAD)

/* per pthread data */
typedef struct {
	pthread_t pthread;	/* The pthread */
	int	  ret;		/* pthread create return */
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
	void 	 *stack;	/* pthread stack */
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

static int stress_set_pthread_max(const char *opt)
{
	uint64_t pthread_max;

	pthread_max = stress_get_uint64(opt);
	stress_check_range("pthread-max", pthread_max,
		MIN_PTHREAD, MAX_PTHREAD);
	return stress_set_setting("pthread-max", TYPE_ID_UINT64, &pthread_max);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_pthread_max,	stress_set_pthread_max },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_PTHREAD)

/* Some systems such as GNU/HURD don't define PTHREAD_STACK_MIN */
#if !defined(PTHREAD_STACK_MIN)
#define PTHREAD_STACK_MIN		(16 * KB)
#endif

#define DEFAULT_STACK_MIN		(16 * KB)

#if defined(HAVE_GET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
static inline long sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr)
{
	return syscall(__NR_get_robust_list, pid, head_ptr, len_ptr);
}
#endif

#if defined(HAVE_SET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
static inline long sys_set_robust_list(struct robust_list_head *head, size_t len)
{
	return syscall(__NR_set_robust_list, head, len);
}
#endif

static inline void stop_running(void)
{
	keep_running_flag = false;
	keep_thread_running_flag = false;
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
	return keep_running() & keep_thread_running_flag;
}

/*
 *  stress_pthread_tid_address()
 *	exercise tid address kernel interfaces. Fetch tid addr
 *	if prctl() allows and set this back using set_tid_address.
 *	Sanity check the current pthread tid is the same as the
 *	one returned by set_tid_address.
 */
static void stress_pthread_tid_address(const stress_args_t *args)
{
#if defined(HAVE_SYS_PRCTL_H) &&	\
    defined(HAVE_PRCTL) &&		\
    defined(PR_GET_TID_ADDRESS) &&	\
    defined(__NR_set_tid_address) &&	\
    defined(HAVE_KERNEL_ULONG_T)
	__kernel_ulong_t tid_addr = 0;

	if (prctl(PR_GET_TID_ADDRESS, &tid_addr) == 0) {
		if (tid_addr) {
			pid_t tid1, tid2;

			/* Nullify */
			tid1 = syscall(__NR_set_tid_address, NULL);
			(void)tid1;

			/* This always succeeds */
			tid1 = syscall(__NR_set_tid_address, tid_addr);

			errno = 0;
			tid2 = shim_gettid();
			if ((errno == 0) && (tid1 != tid2)) {
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
	static void *nowt = NULL;
	int ret;
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
	const stress_args_t *args = ((stress_pthread_args_t *)parg)->args;

#if defined(HAVE_GET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
	/*
	 *  Check that get_robust_list() works OK
	 */
	if (sys_get_robust_list(0, &head, &len) < 0) {
		if (errno != ENOSYS) {
			pr_fail("%s: get_robust_list failed, tid=%d, errno=%d (%s)\n",
				args->name, (int)tid, errno, strerror(errno));
			goto die;
		}
	} else {
#if defined(HAVE_SET_ROBUST_LIST) &&	\
    defined(HAVE_LINUX_FUTEX_H)
		if (sys_set_robust_list(head, len) < 0) {
			if (errno != ENOSYS) {
				pr_fail("%s: set_robust_list failed, tid=%d, errno=%d (%s)\n",
					args->name, (int)tid, errno, strerror(errno));
				goto die;
			}
		}

		/* Exercise invalid zero length */
		ret = sys_set_robust_list(head, 0);
		(void)ret;

		/* Exercise invalid length */
		ret = sys_set_robust_list(head, -1);
		(void)ret;
#endif
	/*
	 *  Check get_robust_list with an invalid PID
	 */
	}
	ret = sys_get_robust_list(-1, &head, &len);
	(void)ret;
#endif

	/*
	 *  Exercise tgkill (no-op on systems that do
	 *  not support this system call)
	 */
	ret = shim_tgkill(tgid, tid, 0);
	(void)ret;

	ret = shim_tgkill(-1, tid, 0);
	(void)ret;

	ret = shim_tgkill(tgid, -1, 0);
	(void)ret;

	ret = shim_tgkill(tgid, tid, -1);
	(void)ret;

	ret = shim_tgkill(stress_get_unused_pid_racy(false), tid, 0);
	(void)ret;

	ret = shim_tgkill(tgid, stress_get_unused_pid_racy(false), 0);
	(void)ret;

	/*
	 *  Exercise tkill, this is either supported directly, emulated
	 *  by tgkill or a no-op if systems don't support tkill or tgkill
	 */
	ret = shim_tkill(tid, 0);
	(void)ret;

	ret = shim_tkill(-1, 0);
	(void)ret;

	ret = shim_tkill(tid, -1);
	(void)ret;

	ret = shim_tkill(stress_get_unused_pid_racy(false), 0);
	(void)ret;

#if defined(HAVE_ASM_LDT_H) && 	\
    defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_USER_DESC) && 	\
    defined(HAVE_GET_THREAD_AREA)
	{
		struct user_desc u_info;

		/*
		 *  Exercise get_thread_area only for x86
		 */
		ret = syscall(__NR_get_thread_area, &u_info);
#if defined(HAVE_GET_THREAD_AREA)
		if (ret == 0) {
			ret = syscall(__NR_set_thread_area, &u_info);
			(void)ret;
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
	if (ret) {
		pr_fail("%s: pthread_spin_lock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}
	pthread_count++;
	ret = shim_pthread_spin_unlock(&spinlock);
	if (ret) {
		pr_fail("%s: pthread_spin_unlock failed, tid=%d, errno=%d (%s)\n",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}

	/*
	 *  Did parent inform pthreads to terminate?
	 */
	if (!keep_thread_running())
		goto die;

	/*
	 *  Wait for controlling thread to
	 *  indicate it is time to die
	 */
	ret = pthread_mutex_lock(&mutex);
	if (ret) {
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
			if (ret != ETIMEDOUT) {
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
	if (ret)
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

		(void)memset(&info, 0, sizeof(info));
		(void)sigemptyset(&mask);
		(void)sigaddset(&mask, SIGUSR1);

		timeout.tv_sec = 0;
		timeout.tv_nsec = 1000000;

		/* Ignore error return, just exercise the call */
		ret = sigtimedwait(&mask, &info, &timeout);
		(void)ret;
	}
#endif

die:
	(void)keep_running();

	stress_pthread_tid_address(args);

	return &nowt;
}

/*
 *  stress_pthread()
 *	stress by creating pthreads
 */
static int stress_pthread(const stress_args_t *args)
{
	bool locked = false;
	uint64_t limited = 0, attempted = 0;
	uint64_t pthread_max = DEFAULT_PTHREAD;
	int ret;
	stress_pthread_args_t pargs = { args, NULL, 0 };
	sigset_t set;
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
#if DEFAULT_STACK_MIN == PTHREAD_STACK_MIN
	const size_t stack_size = PTHREAD_STACK_MIN;
#else
	const size_t stack_size = STRESS_MAXIMUM(DEFAULT_STACK_MIN, PTHREAD_STACK_MIN);
#endif
#endif

	keep_running_flag = true;

	/*
	 *  Block SIGALRM, instead use sigpending
	 *  in pthread or this process to check if
	 *  SIGALRM has been sent.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
	sigprocmask(SIG_BLOCK, &set, NULL);

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
	ret = pthread_mutex_init(&mutex, NULL);
	if (ret) {
		pr_fail("%s: pthread_mutex_init failed, errno=%d (%s)\n",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t i, j;

		keep_thread_running_flag = true;
		pthread_count = 0;

		(void)memset(&pthreads, 0, sizeof(pthreads));
		ret = pthread_mutex_lock(&mutex);
		if (ret) {
			stop_running();
			continue;
		}
		for (i = 0; i < pthread_max; i++)
			pthreads[i].ret = -1;

		for (i = 0; i < pthread_max; i++) {
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			pthread_attr_t attr;
#endif
			pargs.data = (void *)&pthreads[i];

#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			/*
			 *  We mmap our own per pthread stack to ensure we
			 *  have one available before the pthread is started
			 *  and this allows us to exercise the pthread stack
			 *  setting.
			 */
			pthreads[i].stack = mmap(NULL, stack_size, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
			if (pthreads[i].stack == MAP_FAILED)
				break;

			ret = pthread_attr_init(&attr);
			if (ret) {
				pr_fail("%s: pthread_attr_init failed, errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
				break;
			}
			ret = pthread_attr_setstack(&attr, pthreads[i].stack, stack_size);
			if (ret) {
				pr_fail("%s: pthread_attr_setstack failed, errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
				break;
			}
#endif

			pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
				stress_pthread_func, (void *)&pargs);
			if (pthreads[i].ret) {
				/* Out of resources, don't try any more */
				if (pthreads[i].ret == EAGAIN) {
					limited++;
					break;
				}
				/* Something really unexpected */
				pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
					args->name, pthreads[i].ret, strerror(ret));
				stop_running();
				break;
			}
			inc_counter(args);
			if (!(keep_running() && keep_stressing(args)))
				break;
		}
		attempted++;
		ret = pthread_mutex_unlock(&mutex);
		if (ret) {
			stop_running();
			goto reap;

		}
		/*
		 *  Wait until they are all started or
		 *  we get bored waiting..
		 */
		for (j = 0; j < 1000; j++) {
			bool all_running = false;

			if (!keep_stressing(args)) {
				stop_running();
				goto reap;
			}

			if (!locked) {
				ret = shim_pthread_spin_lock(&spinlock);
				if (ret) {
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
				if (ret) {
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

			(void)memset(&value, 0, sizeof(value));
			ret = pthread_sigqueue(pthreads[j].pthread, SIGUSR1, value);
			(void)ret;
		}
#endif

reap:
		keep_thread_running_flag = false;
		ret = pthread_cond_broadcast(&cond);
		if (ret) {
			pr_fail("%s: pthread_cond_broadcast failed (parent), errno=%d (%s)\n",
				args->name, ret, strerror(ret));
			stop_running();
			/* fall through and unlock */
		}
		for (j = 0; j < i; j++) {
#if defined(HAVE_PTHREAD_ATTR_SETSTACK)
			if (pthreads[j].stack != MAP_FAILED)
				(void)munmap(pthreads[j].stack, stack_size);
#endif
			if (pthreads[j].ret)
				continue;
			ret = pthread_join(pthreads[j].pthread, NULL);
			if ((ret) && (ret != ESRCH)) {
				pr_fail("%s: pthread_join failed (parent), errno=%d (%s)\n",
					args->name, ret, strerror(ret));
				stop_running();
			}
		}
	} while (!locked && keep_running() && keep_stressing(args));

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)attempted,
			pthread_max, args->instance);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)pthread_cond_destroy(&cond);
	(void)pthread_mutex_destroy(&mutex);
	(void)shim_pthread_spin_destroy(&spinlock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_pthread_info = {
	.stressor = stress_pthread,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_pthread_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
