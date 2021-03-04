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
	{ NULL,	"sigpending N",     "start N workers exercising sigpending" },
	{ NULL,	"sigpending-ops N", "stop after N sigpending bogo operations" },
	{ NULL,	NULL,		    NULL }
};

/*
 *  stress_usr1_handler()
 *	SIGUSR1 signal handler
 */
static void MLOCKED_TEXT stress_usr1_handler(int signum)
{
	(void)signum;
}

/*
 *  stress_sigpending
 *	stress sigpending system call
 */
static int stress_sigpending(const stress_args_t *args)
{
	sigset_t _sigset;

	if (stress_sighandler(args->name, SIGUSR1, stress_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)sigemptyset(&_sigset);
		(void)sigaddset(&_sigset, SIGUSR1);
		if (sigprocmask(SIG_SETMASK, &_sigset, NULL) < 0) {
			pr_fail("%s: sigprocmask failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		(void)kill(args->pid, SIGUSR1);
		if (sigpending(&_sigset) < 0) {
			pr_fail("%s: sigpending failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			continue;
		}
		/* We should get a SIGUSR1 here */
		if (!sigismember(&_sigset, SIGUSR1)) {
			pr_fail("%s: did not get a SIGUSR1 pending signal\n", args->name);
			continue;
		}

		/* Unmask signal, signal is handled */
		(void)sigemptyset(&_sigset);
		(void)sigprocmask(SIG_SETMASK, &_sigset, NULL);

		/* And it is no longer pending */
		if (sigpending(&_sigset) < 0) {
			pr_fail("%s: got an unexpected SIGUSR1 pending signal\n", args->name);
			continue;
		}
		if (sigismember(&_sigset, SIGUSR1)) {
			pr_fail("%s: got an unexpected SIGUSR1 signal\n", args->name);
			continue;
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigpending_info = {
	.stressor = stress_sigpending,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
