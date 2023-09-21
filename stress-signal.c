// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"signal N",	"start N workers that exercise signal" },
	{ NULL,	"signal-ops N",	"stop after N bogo signals" },
	{ NULL,	NULL,		 NULL }
};

static volatile uint64_t counter;

static void MLOCKED_TEXT stress_signal_handler(int signum)
{
	(void)signum;

	counter++;
}

typedef void (*shim_sighandler_t)(int);

/*
 *  shim_signal()
 *	C libraries wrap signal and use the POSIX sigaction system
 *	call instead.  If we have the signal system call then call it
 *	directly with the default BSD or POSIX behaviour depending on
 *	underlying kernel implementation.
 */
static inline shim_sighandler_t shim_signal(int signum, shim_sighandler_t handler)
{
#if defined(__NR_signal) &&	\
    defined(HAVE_SYSCALL) &&	\
    !defined(__sparc__)
	shim_sighandler_t ret;

	ret = (shim_sighandler_t)syscall(__NR_signal, signum, handler);
	if (UNLIKELY((ret == SIG_ERR) && (errno == ENOSYS))) {
		errno = 0;
		ret = signal(signum, handler);
	}
	return ret;
#else
	return signal(signum, handler);
#endif
}

/*
 *  stress_signal
 *	stress by generating SIGCHLD signals on exiting
 *	child processes.
 */
static int stress_signal(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	const pid_t pid = getpid();

	counter = 0;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t tmp;

		tmp = counter;
		if (UNLIKELY(shim_signal(SIGCHLD, SIG_IGN) == SIG_ERR)) {
			pr_fail("%s: cannot install SIGCHLD SIG_IGN handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (UNLIKELY(tmp != counter)) {
			pr_err("%s: setting of SIG_IGN unexpectedly triggered a SIGCHLD\n",
				args->name);
		}

		tmp = counter;
		if (UNLIKELY(shim_signal(SIGCHLD, stress_signal_handler) == SIG_ERR)) {
			pr_fail("%s: cannot install SIGCHLD signal handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (UNLIKELY(tmp != counter)) {
			pr_err("%s: setting of SIGIGN unexpectedly triggered a SIGCHLD\n",
				args->name);
		}

		tmp = counter;
		if (LIKELY(shim_kill(pid, SIGCHLD) == 0)) {
			while ((tmp == counter) && stress_continue_flag()) {
				shim_sched_yield();
			}
		}

		tmp = counter;
		if (UNLIKELY(shim_signal(SIGCHLD, SIG_DFL) == SIG_ERR)) {
			pr_fail("%s: cannot install SIGCHLD SIG_DFL handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (UNLIKELY(tmp != counter)) {
			pr_fail("%s: setting of SIG_DFL unexpectedly triggered a SIGCHLD\n",
				args->name);
			rc = EXIT_FAILURE;
		}

		stress_bogo_set(args, counter);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_signal_info = {
	.stressor = stress_signal,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
