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

static const stress_help_t help[] = {
	{ NULL,	"sigpending N",     "start N workers exercising sigpending" },
	{ NULL,	"sigpending-ops N", "stop after N sigpending bogo operations" },
	{ NULL,	NULL,		    NULL }
};

/*
 *  stress_sigpending
 *	stress sigpending system call
 */
static int stress_sigpending(stress_args_t *args)
{
	sigset_t new_sigset ALIGN64, old_sigset ALIGN64;

	if (stress_sighandler(args->name, SIGUSR1, stress_sighandler_nop, NULL) < 0)
		return EXIT_FAILURE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)sigemptyset(&new_sigset);
		(void)sigaddset(&new_sigset, SIGUSR1);
		if (UNLIKELY(sigprocmask(SIG_SETMASK, &new_sigset, NULL) < 0)) {
			pr_fail("%s: sigprocmask failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		(void)shim_kill(args->pid, SIGUSR1);
		if (UNLIKELY(sigpending(&new_sigset) < 0)) {
			pr_fail("%s: sigpending failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			continue;
		}
		/* We should get a SIGUSR1 here */
		if (UNLIKELY(!sigismember(&new_sigset, SIGUSR1))) {
			pr_fail("%s: did not get a SIGUSR1 pending signal\n", args->name);
			continue;
		}

		/* Unmask signal, signal is handled */
		(void)sigemptyset(&new_sigset);
		(void)sigprocmask(SIG_SETMASK, &new_sigset, NULL);

		/* And it is no longer pending */
		if (UNLIKELY(sigpending(&new_sigset) < 0)) {
			pr_fail("%s: got an unexpected SIGUSR1 pending signal\n", args->name);
			continue;
		}
		if (UNLIKELY(sigismember(&new_sigset, SIGUSR1))) {
			pr_fail("%s: got an unexpected SIGUSR1 signal\n", args->name);
			continue;
		}

		/* Exercise invalid sigprocmask how argument */
		(void)sigprocmask(~0, &new_sigset, NULL);

		/* Fetch existing sigset */
		(void)sigemptyset(&old_sigset);
		(void)sigprocmask(SIG_SETMASK, NULL, &old_sigset);

		/* Do nothing */
		(void)sigprocmask(SIG_SETMASK, NULL, NULL);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_sigpending_info = {
	.stressor = stress_sigpending,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
