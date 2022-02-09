/*
 * Copyright (C)      2022 Colin Ian King.
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

#if defined(SA_SIGINFO) &&	\
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

static siginfo_t siginfo;
static char selector;

static const stress_help_t help[] = {
	{ NULL,	"sigsusr N",	 "start N workers exercising a userspace system call handler" },
	{ NULL,	"sigsusr-ops N", "stop after N successful SIGSYS system callls" },
	{ NULL,	NULL,		 NULL }
};

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

/*
 *  stress_sigsys_handler()
 *	SIGSYS handler
 */
static void MLOCKED_TEXT stress_sigsys_handler(
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
static int stress_usersyscall(const stress_args_t *args)
{
	int ret;
	struct sigaction action;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)memset(&action, 0, sizeof action);
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
		pr_fail("%s: sigaction SIGSYS: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	do {
		/*
		 *  Test case 1: call user syscall with
		 *  dispatcher disabled
		 */
		dispatcher_off();
		ret = prctl(PR_SET_SYSCALL_USER_DISPATCH,
			    PR_SYS_DISPATCH_ON, 0, 0, &selector);
		if (ret) {
			pr_inf("%s: user dispatch failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		/*  Expect ENOSYS for the system call return */
		ret = syscall(USR_SYSCALL);
		if (errno != ENOSYS) {
			pr_err("%s: didn't get ENOSYS on user syscall, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}

		/*
		 *  Test case 2: call user syscall with
		 *  dispatcher enabled
		 */
		(void)memset(&siginfo, 0, sizeof(siginfo));
		dispatcher_on();
		ret = syscall(USR_SYSCALL);
		dispatcher_off();
		/*  Should return USR_SYSCALL */
		if (ret != USR_SYSCALL) {
			pr_err("%s: didn't get 0x%x on user syscall, "
				"got 0x%x instead, errno=%d (%s)\n",
				args->name, USR_SYSCALL, ret, errno, strerror(errno));
			continue;
		}
		/* check handler si_code */
		if (siginfo.si_code != SYS_USER_DISPATCH) {
			pr_err("%s: didn't get SYS_USER_DISPATCH in siginfo.si_code, "
				"got 0x%x instead\n", args->name, siginfo.si_code);
			continue;
		}
		/* check handler si_error */
		if (siginfo.si_errno != 0) {
			pr_err("%s: didn't get 0x0 in siginfo.si_errno, "
				"got 0x%x instead\n", args->name, siginfo.si_errno);
			continue;
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_usersyscall_info = {
	.stressor = stress_usersyscall,
	.class = CLASS_OS,
	.supported = stress_supported,
	.help = help
};

#else

stressor_info_t stress_usersyscall_info = {
        .stressor = stress_not_implemented,
	.class = CLASS_OS,
        .help = help
};

#endif
