/*
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

static const stress_help_t help[] = {
	{ NULL,	"usersyscall N",	"start N workers exercising a userspace system call handler" },
	{ NULL,	"usersyscall-ops N", 	"stop after N successful SIGSYS system calls" },
	{ NULL,	NULL,		 	NULL }
};

#if defined(SA_SIGINFO) &&	\
    defined(HAVE_SYSCALL) &&	\
    defined(HAVE_SYS_PRCTL_H)

#include <sys/prctl.h>

#if !defined(PR_SET_SYSCALL_USER_DISPATCH)
#define PR_SET_SYSCALL_USER_DISPATCH	(59)
#endif

#if !defined(PR_SYS_DISPATCH_OFF)
#define PR_SYS_DISPATCH_OFF		(0)
#endif
#if !defined(PR_SYS_DISPATCH_ON)
#define PR_SYS_DISPATCH_ON		(1)
#endif

#if !defined(SYSCALL_DISPATCH_FILTER_ALLOW)
#define SYSCALL_DISPATCH_FILTER_ALLOW	(0)
#endif

#if !defined(SYSCALL_DISPATCH_FILTER_BLOCK)
#define SYSCALL_DISPATCH_FILTER_BLOCK	(1)
#endif

#if !defined(SYS_USER_DISPATCH)
#define SYS_USER_DISPATCH		(2)
#endif

#if defined(__NR_syscalls)
#define USR_SYSCALL		(__NR_syscalls + 256)
#else
#define USR_SYSCALL		(0xe000)
#endif

#if defined(__linux__) &&       	\
    !defined(HAVE_COMPILER_TCC) &&     	\
    !defined(HAVE_COMPILER_PCC) &&	\
   defined(STRESS_ARCH_X86_64)
#define STRESS_EXERCISE_X86_SYSCALL
#endif

#define X_STR_(x) #x
#define X_STR(x) X_STR_(x)

static siginfo_t siginfo;
static char selector;

static inline void dispatcher_off(void)
{
	selector = SYSCALL_DISPATCH_FILTER_ALLOW;
}

static inline void dispatcher_on(void)
{
	selector = SYSCALL_DISPATCH_FILTER_BLOCK;
}

static int stress_supported(const char *name)
{
	int ret;

	dispatcher_off();
	ret = prctl(PR_SET_SYSCALL_USER_DISPATCH,
		    PR_SYS_DISPATCH_ON, 0, 0, &selector);
	if (ret) {
		pr_inf_skip("%s: prctl user dispatch is not working, skipping the stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(STRESS_EXERCISE_X86_SYSCALL)
/*
 *  x86_64_syscall0()
 *      syscall 0 arg wrapper
 */
static inline long int x86_64_syscall0(const long int number)
{
	register long int ret;

	__asm__ __volatile__("syscall\n\t"
			: "=a" (ret)
			: "0" (number)
			: "memory", "cc", "r11", "cx");
	if (UNLIKELY(ret < 0)) {
		errno = (int)-ret;
		ret = -1;
	}
	return ret;
}

/*
 *  stress_sigsys_libc_mapping()
 *	find address of libc text segment by scanning /proc/self/maps
 */
static int stress_sigsys_libc_mapping(uintptr_t *begin, uintptr_t *end)
{
	char perm[5], buf[1024] ALIGN64;
	char libc_path[PATH_MAX + 1];
	FILE *fp;
	uint64_t offset, dev_major, dev_minor, inode;

	fp = fopen("/proc/self/maps", "r");
	if (UNLIKELY(!fp))
		goto err;

	*begin = ~(uintptr_t)0;
	*end = 0;

	while (fgets(buf, sizeof(buf), fp)) {
		int n;
		uintptr_t map_begin, map_end;

		n = sscanf(buf, "%" SCNxPTR "-%" SCNxPTR "%4s %" SCNx64 " %" SCNx64
			":%" SCNx64 " %" SCNu64 "%" X_STR(PATH_MAX) "s\n",
			&map_begin, &map_end, perm, &offset, &dev_major, &dev_minor,
			&inode, libc_path);

		/*
		 *  name /libc-*.so or /libc.so found?
		 */
		if ((n == 8) && !strncmp(perm, "r-xp", 4) &&
		    strstr(libc_path, ".so")) {
			if (strstr(libc_path, "/libc-") ||
			    strstr(libc_path, "/libc.so")) {
				if (*begin > map_begin)
					*begin = map_begin;
				if (*end < map_end)
					*end = map_end;
			}
		}
	}
	(void)fclose(fp);

	if ((*begin == ~(uintptr_t)0) || (*end == 0))
		goto err;

	return 0;

err:
	*begin = 0;
	*end = 0;
	return -1;
}
#endif

/*
 *  stress_sigsys_handler()
 *	SIGSYS handler
 */
static void MLOCKED_TEXT OPTIMIZE3 stress_sigsys_handler(
	int num,
	siginfo_t *info,
	void *ucontext)
{
	(void)num;
	(void)ucontext;

	dispatcher_off();

	/* Copy siginfo for examination by caller */
	if (LIKELY(info != NULL))
		siginfo = *info;
}

/*
 *  stress_usersyscall
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int OPTIMIZE3 stress_usersyscall(stress_args_t *args)
{
	int ret, rc;
	struct sigaction action;
#if defined(STRESS_EXERCISE_X86_SYSCALL)
	uintptr_t begin, end;
	const bool libc_ok = (stress_sigsys_libc_mapping(&begin, &end) == 0);
	const pid_t pid = getpid();
#endif
	double duration = 0.0, count = 0.0, rate;
	int metrics_count = 0;

	(void)shim_memset(&action, 0, sizeof action);
	action.sa_sigaction = stress_sigsys_handler;
	(void)sigemptyset(&action.sa_mask);
	/*
	 *  We need to block a range of signals from occurring
	 *  while we are handling the SIGSYS to avoid any
	 *  system calls that would cause a nested SIGSYS.
	 */
	(void)sigfillset(&action.sa_mask);
	(void)sigdelset(&action.sa_mask, SIGSYS);
	action.sa_flags = SA_SIGINFO;

	ret = sigaction(SIGSYS, &action, NULL);
	if (ret < 0) {
		pr_fail("%s: sigaction SIGSYS failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)shim_memset(&siginfo, 0, sizeof(siginfo));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		/*
		 *  Test case 1: call user syscall with
		 *  dispatcher disabled
		 */
		dispatcher_off();
		ret = prctl(PR_SET_SYSCALL_USER_DISPATCH,
			    PR_SYS_DISPATCH_ON, 0, 0, &selector);
		if (UNLIKELY(ret)) {
			pr_inf("%s: user dispatch failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		/*  Expect ENOSYS for the system call return */
		errno = 0;
		VOID_RET(long int, syscall(USR_SYSCALL));
		if (UNLIKELY(errno != ENOSYS)) {
			pr_fail("%s: didn't get ENOSYS on user syscall, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}

		/*
		 *  Test case 2: call user syscall with
		 *  dispatcher enabled
		 */
		if (LIKELY(metrics_count++ < 1000)) {
			dispatcher_on();
			ret = (int)syscall(USR_SYSCALL);
			dispatcher_off();
		} else {
			double t;

			metrics_count = 0;

			t = stress_time_now();
			dispatcher_on();
			ret = (int)syscall(USR_SYSCALL);
			dispatcher_off();
			duration += stress_time_now() - t;
			count += 1.0;
		}
		/*  Should return USR_SYSCALL */
		if (UNLIKELY(ret != USR_SYSCALL)) {
			if (errno == ENOSYS) {
				pr_inf_skip("%s: got ENOSYS for usersyscall, skipping stressor\n", args->name);
				rc = EXIT_NOT_IMPLEMENTED;
				goto err;
			}
			pr_fail("%s: didn't get 0x%x on user syscall, "
				"got 0x%x instead, errno=%d (%s)\n",
				args->name, USR_SYSCALL, ret, errno, strerror(errno));
			continue;
		}
		/* check handler si_code */
		if (UNLIKELY(siginfo.si_code != SYS_USER_DISPATCH)) {
			pr_fail("%s: didn't get SYS_USER_DISPATCH in siginfo.si_code, "
				"got 0x%x instead\n", args->name, siginfo.si_code);
			continue;
		}
		/* check handler si_error */
		if (UNLIKELY(siginfo.si_errno != 0)) {
			pr_fail("%s: didn't get 0x0 in siginfo.si_errno, "
				"got 0x%x instead\n", args->name, siginfo.si_errno);
			continue;
		}
		VOID_RET(int, prctl(PR_SET_SYSCALL_USER_DISPATCH,
			    PR_SYS_DISPATCH_OFF, 0, 0, 0));

#if defined(STRESS_EXERCISE_X86_SYSCALL)
		if (libc_ok) {
			int saved_errno, ret_not_libc;
			const int ret_libc = (int)syscall(__NR_getpid);

			/*
			 *  Test case 3: call syscall with libc syscall bounds.
			 *  All libc system calls don't get handled by the
			 *  dispatch signal handler, all non-libc system calls
			 *  get handled by SIGSYS.
			 */
			ret = prctl(PR_SET_SYSCALL_USER_DISPATCH,
				    PR_SYS_DISPATCH_ON, begin, end - begin, &selector);
			if (UNLIKELY(ret != 0)) {
				pr_inf("%s: user dispatch failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}

			/*
			 *  getpid via non-libc syscall, will be handled by SIGSYS
			 */
			errno = 0;
			dispatcher_on();
			ret_not_libc = (int)x86_64_syscall0(__NR_getpid);
			saved_errno = errno;
			dispatcher_off();

			VOID_RET(int, prctl(PR_SET_SYSCALL_USER_DISPATCH,
				    PR_SYS_DISPATCH_OFF, 0, 0, 0));

			if (UNLIKELY(ret_libc != pid)) {
				pr_fail("%s: didn't get PID on libc getpid syscall, "
					"got %d instead, errno=%d (%s)\n",
					args->name, ret_libc,
					saved_errno, strerror(saved_errno));
			}

			if (UNLIKELY(ret_not_libc != __NR_getpid)) {
				pr_fail("%s: didn't get __NR_getpid %x on user syscall, "
					"got 0x%x instead, errno=%d (%s)\n",
					args->name, __NR_getpid, ret_not_libc,
					saved_errno, strerror(saved_errno));
			}
		}
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
	rate = (count > 0.0) ? duration / count : 0.0;
	stress_metrics_set(args, 0, "nanosecs per syscall",
		rate * STRESS_DBL_NANOSECOND, STRESS_METRIC_HARMONIC_MEAN);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_usersyscall_info = {
	.stressor = stress_usersyscall,
	.classifier = CLASS_OS,
	.supported = stress_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_usersyscall_info = {
        .stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
        .help = help,
	.unimplemented_reason = "only supported on Linux",
};

#endif
