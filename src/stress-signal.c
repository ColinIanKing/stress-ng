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
static shim_sighandler_t shim_signal(int signum, shim_sighandler_t handler)
{
#if defined(__NR_signal)
	return (shim_sighandler_t)syscall(__NR_signal, signum, handler);
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
		if (shim_signal(SIGCHLD, SIG_IGN) == SIG_ERR) {
			pr_err("%s: cannot install SIGCHLD SIG_IGN handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (tmp != counter) {
			pr_err("%s: setting of SIG_IGN unexpectedly triggered a SIGCHLD\n",
				args->name);
		}

		tmp = counter;
		if (shim_signal(SIGCHLD, stress_signal_handler) == SIG_ERR) {
			pr_err("%s: cannot install SIGCHLD signal handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (tmp != counter) {
			pr_err("%s: setting of SIGIGN unexpectedly triggered a SIGCHLD\n",
				args->name);
		}

		tmp = counter;
		if (kill(pid, SIGCHLD) == 0) {
			while ((tmp == counter) && keep_stressing(args)) {
				shim_sched_yield();
			}
		}

		tmp = counter;
		if (shim_signal(SIGCHLD, SIG_DFL) == SIG_ERR) {
			pr_err("%s: cannot install SIGCHLD SIG_DFL handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}
		if (tmp != counter) {
			pr_err("%s: setting of SIG_DFL unexpectedly triggered a SIGCHLD\n",
				args->name);
		}

		set_counter(args, counter);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_signal_info = {
	.stressor = stress_signal,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
