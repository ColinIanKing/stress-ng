/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"pthread N",	 "start N workers that create multiple threads" },
	{ NULL,	"pthread-ops N", "stop pthread workers after N bogo threads created" },
	{ NULL,	"pthread-max P", "create P threads at a time by each worker" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_LIB_PTHREAD)

typedef struct {
	pthread_t pthread;
	pid_t     tid;
} pthread_info_t;

static pthread_cond_t cond;
static pthread_mutex_t mutex;
static shim_pthread_spinlock_t spinlock;
static volatile bool keep_thread_running_flag;
static volatile bool keep_running_flag;
static uint64_t pthread_count;
static pthread_info_t pthreads[MAX_PTHREAD];

#endif

static int stress_set_pthread_max(const char *opt)
{
	uint64_t pthread_max;

	pthread_max = get_uint64(opt);
	check_range("pthread-max", pthread_max,
		MIN_PTHREAD, MAX_PTHREAD);
	return set_setting("pthread-max", TYPE_ID_UINT64, &pthread_max);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_pthread_max,	stress_set_pthread_max },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_PTHREAD)

#if defined(HAVE_GET_ROBUST_LIST) && defined(HAVE_LINUX_FUTEX_H)
static inline long sys_get_robust_list(int pid, struct robust_list_head **head_ptr, size_t *len_ptr)
{
	return syscall(__NR_get_robust_list, pid, head_ptr, len_ptr);
}
#endif

#if defined(HAVE_SET_ROBUST_LIST) && defined(HAVE_LINUX_FUTEX_H)
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
 *  stress_pthread_func()
 *	pthread that exits immediately
 */
static void *stress_pthread_func(void *parg)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	static void *nowt = NULL;
	int ret;
	const pid_t tid = shim_gettid();
#if defined(HAVE_GET_ROBUST_LIST) && defined(HAVE_LINUX_FUTEX_H)
	struct robust_list_head *head;
	size_t len;
#endif
	const args_t *args = ((pthread_args_t *)parg)->args;

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		goto die;

#if defined(HAVE_GETTID)
	{
		pthread_info_t *pi = ((pthread_args_t *)parg)->data;
		pi->tid = shim_gettid();
	}
#endif

#if defined(HAVE_GET_ROBUST_LIST) && defined(HAVE_LINUX_FUTEX_H)
	/*
	 *  Check that get_robust_list() works OK
	 */
	if (sys_get_robust_list(0, &head, &len) < 0) {
		if (errno != ENOSYS) {
			pr_fail("%s: get_robust_list failed, tid=%d, errno=%d (%s)",
				args->name, tid, errno, strerror(errno));
			goto die;
		}
	} else {
#if defined(HAVE_SET_ROBUST_LIST) && defined(HAVE_LINUX_FUTEX_H)
		if (sys_set_robust_list(head, len) < 0) {
			if (errno != ENOSYS) {
				pr_fail("%s: set_robust_list failed, tid=%d, errno=%d (%s)",
					args->name, tid, errno, strerror(errno));
				goto die;
			}
		}
#endif
	}
#endif

	/*
	 *  Bump count of running threads
	 */
	ret = shim_pthread_spin_lock(&spinlock);
	if (ret) {
		pr_fail("%s: pthread_spin_lock failed, tid=%d, errno=%d (%s)",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}
	pthread_count++;
	ret = shim_pthread_spin_unlock(&spinlock);
	if (ret) {
		pr_fail("%s: pthread_spin_unlock failed, tid=%d, errno=%d (%s)",
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
		pr_fail("%s: pthread_mutex_lock failed, tid=%d, errno=%d (%s)",
			args->name, (int)tid, ret, strerror(ret));
		goto die;
	}
	while (keep_thread_running()) {
		ret = pthread_cond_wait(&cond, &mutex);
		if (ret) {
			pr_fail("%s: pthread_cond_wait failed, tid=%d, errno=%d (%s)",
				args->name, (int)tid, ret, strerror(ret));
			break;
		}
		(void)shim_sched_yield();
	}
	ret = pthread_mutex_unlock(&mutex);
	if (ret)
		pr_fail("%s: pthread_mutex_unlock failed, tid=%d, errno=%d (%s)",
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
die:
	(void)keep_running();
	return &nowt;
}

/*
 *  stress_pthread()
 *	stress by creating pthreads
 */
static int stress_pthread(const args_t *args)
{
	bool locked = false;
	uint64_t limited = 0, attempted = 0;
	uint64_t pthread_max = DEFAULT_PTHREAD;
	int ret;
	pthread_args_t pargs = { args, NULL };
	sigset_t set;

	keep_running_flag = true;

	/*
	 *  Block SIGALRM and SIGUSR2, instead
	 *  use sigpending in pthread or this process
	 *  to check if SIGALRM has been sent.
	 */
	sigemptyset(&set);
	sigaddset(&set, SIGALRM);
#if defined(SIGUSR2)
	sigaddset(&set, SIGUSR2);
#endif
	sigprocmask(SIG_BLOCK, &set, NULL);

	if (!get_setting("pthread-max", &pthread_max)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			pthread_max = MAX_PTHREAD;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			pthread_max = MIN_PTHREAD;
	}

	ret = pthread_cond_init(&cond, NULL);
	if (ret) {
		pr_fail("%s: pthread_cond_init failed, errno=%d (%s)",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}
	ret = shim_pthread_spin_init(&spinlock, SHIM_PTHREAD_PROCESS_SHARED);
	if (ret) {
		pr_fail("%s: pthread_spin_init failed, errno=%d (%s)",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}
	ret = pthread_mutex_init(&mutex, NULL);
	if (ret) {
		pr_fail("%s: pthread_mutex_init failed, errno=%d (%s)",
			args->name, ret, strerror(ret));
		return EXIT_FAILURE;
	}

	do {
		uint64_t i, j;

		keep_thread_running_flag = true;
		pthread_count = 0;

		(void)memset(&pthreads, 0, sizeof(pthreads));

		for (i = 0; i < pthread_max; i++) {
			pargs.data = (void *)&pthreads[i];

			ret = pthread_create(&pthreads[i].pthread, NULL,
				stress_pthread_func, (void *)&pargs);
			if (ret) {
				/* Out of resources, don't try any more */
				if (ret == EAGAIN) {
					limited++;
					break;
				}
				/* Something really unexpected */
				pr_fail("%s: pthread_create failed, errno=%d (%s)",
					args->name, ret, strerror(ret));
				stop_running();
				break;
			}
			inc_counter(args);
			if (!(keep_running() && keep_stressing()))
				break;
		}
		attempted++;

		/*
		 *  Wait until they are all started or
		 *  we get bored waiting..
		 */
		for (j = 0; j < 1000; j++) {
			bool all_running = false;

			if (!locked) {
				ret = pthread_mutex_lock(&mutex);
				if (ret) {
					pr_fail("%s: pthread_mutex_lock failed (parent), errno=%d (%s)",
						args->name, ret, strerror(ret));
					stop_running();
					goto reap;
				}
				locked = true;
			}
			all_running = (pthread_count == i);

			if (locked) {
				ret = pthread_mutex_unlock(&mutex);
				if (ret) {
					pr_fail("%s: pthread_mutex_unlock failed (parent), errno=%d (%s)",
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

#if defined(HAVE_TGKILL) && defined(SIGUSR2)
		for (j = 0; j < i; j++) {
			if (pthreads[j].tid)
				(void)syscall(__NR_tgkill, args->pid, pthreads[j].tid, SIGUSR2);
		}
#endif
reap:
		keep_thread_running_flag = false;
		ret = pthread_cond_broadcast(&cond);
		if (ret) {
			pr_fail("%s: pthread_cond_broadcast failed (parent), errno=%d (%s)",
				args->name, ret, strerror(ret));
			stop_running();
			/* fall through and unlock */
		}
		for (j = 0; j < i; j++) {
			ret = pthread_join(pthreads[j].pthread, NULL);
			if ((ret) && (ret != ESRCH)) {
				pr_fail("%s: pthread_join failed (parent), errno=%d (%s)",
					args->name, ret, strerror(ret));
				stop_running();
			}
		}
	} while (!locked && keep_running() && keep_stressing());

	if (limited) {
		pr_inf("%s: %.2f%% of iterations could not reach "
			"requested %" PRIu64 " threads (instance %"
			PRIu32 ")\n",
			args->name,
			100.0 * (double)limited / (double)attempted,
			pthread_max, args->instance);
	}

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
