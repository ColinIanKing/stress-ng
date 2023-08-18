// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

static const stress_help_t help[] = {
	{ NULL,	"sigq N",	"start N workers sending sigqueue signals" },
	{ NULL,	"sigq-ops N",	"stop after N sigqueue bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SIGQUEUE) && \
    defined(HAVE_SIGWAITINFO) && \
    defined(SA_SIGINFO)

volatile bool handled_sigchld;

static void MLOCKED_TEXT stress_sigqhandler(
	int sig,
	siginfo_t *info,
	void *ucontext)
{
	(void)sig;
	(void)info;
	(void)ucontext;
}

static void MLOCKED_TEXT stress_sigq_chld_handler(int sig)
{
	if (sig == SIGCHLD) {
		handled_sigchld = true;
		stress_continue_set_flag(false);
	}
}

#if defined(__NR_rt_sigqueueinfo) &&	\
    defined(HAVE_SYSCALL)
#define HAVE_RT_SIGQUEUEINFO
static int shim_rt_sigqueueinfo(pid_t tgid, int sig, siginfo_t *info)
{
	return (int)syscall(__NR_rt_sigqueueinfo, tgid, sig, info);
}
#endif

/*
 *  stress_sigq
 *	stress by heavy sigqueue message sending
 */
static int stress_sigq(const stress_args_t *args)
{
	pid_t pid;
	struct sigaction sa;
#if defined(HAVE_RT_SIGQUEUEINFO)
	const pid_t mypid = getpid();
	const uid_t myuid = getuid();
#endif
	int rc = EXIT_SUCCESS, parent_cpu;
	int val = stress_mwc32();

	if (val == 0)
		val++;

	handled_sigchld = false;

	if (stress_sighandler(args->name, SIGCHLD, stress_sigq_chld_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	(void)shim_memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sigqhandler;
	sa.sa_flags = SA_SIGINFO;

	if (sigaction(SIGUSR1, &sa, NULL) < 0) {
		pr_err("%s: cannot install SIGUSR1, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (!stress_continue(args))
			goto finish;
		if (stress_redo_fork(args, errno))
			goto again;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		sigset_t mask;
		int i = 0;

		(void)stress_change_cpu(args, parent_cpu);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		(void)sigemptyset(&mask);
		(void)sigaddset(&mask, SIGUSR1);
		if (sigprocmask(SIG_SETMASK, &mask, NULL) < 0) {
			pr_err("%s: sigprocmask failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			_exit(EXIT_FAILURE);
		}

		while (stress_continue(args)) {
			siginfo_t info;
			int ret;

			(void)shim_memset(&info, 0, sizeof(info));

			if (i++ & 1) {
				ret = sigwaitinfo(&mask, &info);
				if (ret < 0)
					break;
			} else {
				struct timespec timeout;

				timeout.tv_sec = 1;
				timeout.tv_nsec = 0;

				ret = sigtimedwait(&mask, &info, &timeout);
				if (ret < 0) {
					if (errno == EAGAIN)
						continue;
					break;
				}
			}
			if (UNLIKELY(info.si_value.sival_int != val)) {
				if (UNLIKELY(info.si_value.sival_int != 0)) {
					pr_fail("%s: got unexpected sival_int value, got 0x%x, expecting 0x%x\n",
						args->name, info.si_value.sival_int, val);
					rc = EXIT_FAILURE;
				}
				break;
			}
			if (info.si_signo != SIGUSR1)
				break;
		}
		pr_dbg("%s: child got termination notice\n", args->name);
		pr_dbg("%s: exited on pid [%d] (instance %" PRIu32 ")\n",
			args->name, (int)getpid(), args->instance);
		_exit(rc);
	} else {
		/* Parent */
		union sigval s;
		int status;

		do {
			(void)shim_memset(&s, 0, sizeof(s));
			s.sival_int = val;
			(void)sigqueue(pid, SIGUSR1, s);

#if defined(HAVE_RT_SIGQUEUEINFO)
			{
				siginfo_t info;

				/*
				 *  Invalid si_code, the kernel should return
				 *  -EPERM on Linux.
				 */
				(void)shim_memset(&info, 0, sizeof(info));
				info.si_signo = SIGUSR1;
				info.si_code = SI_TKILL;
				info.si_pid = mypid;
				info.si_uid = myuid;
				info.si_value = s;
				(void)shim_rt_sigqueueinfo(pid, SIGUSR1, &info);

				/* Exercise invalid signal */
				(void)shim_rt_sigqueueinfo(pid, 0, &info);
			}
#endif
			stress_bogo_inc(args);
		} while (stress_continue(args));

		if (!handled_sigchld) {
			pr_dbg("%s: parent sent termination notice\n", args->name);
			(void)shim_memset(&s, 0, sizeof(s));
			s.sival_int = 0;
			(void)sigqueue(pid, SIGUSR1, s);
			(void)shim_usleep(250);
			/* And ensure child is really dead */
		}
		(void)shim_kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
		if (WIFEXITED(status))
			if (WEXITSTATUS(status) == EXIT_FAILURE)
				rc = EXIT_FAILURE;
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_sigq_info = {
	.stressor = stress_sigq,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sigq_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sigqueue() or sigwaitinfo() or defined SA_SIGINFO"
};
#endif
