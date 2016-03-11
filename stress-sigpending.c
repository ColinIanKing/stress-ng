/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>

#include "stress-ng.h"

/*
 *  stress_usr1_handler()
 *	SIGUSR1 signal handler
 */
static void MLOCKED stress_usr1_handler(int dummy)
{
	(void)dummy;
}

/*
 *  stress_sigpending
 *	stress sigpending system call
 */
int stress_sigpending(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	sigset_t sigset;
	const pid_t mypid = getpid();

	(void)instance;

	if (stress_sighandler(name, SIGUSR1, stress_usr1_handler, NULL) < 0)
		return EXIT_FAILURE;

	do {
		sigemptyset(&sigset);
		sigaddset(&sigset, SIGUSR1);
		if (sigprocmask(SIG_SETMASK, &sigset, NULL) < 0) {
			pr_fail_err(name, "sigprocmask");
			return EXIT_FAILURE;
		}

		(void)kill(mypid, SIGUSR1);
		if (sigpending(&sigset) < 0) {
			pr_fail_err(name, "sigpending");
			continue;
		}
		/* We should get a SIGUSR1 here */
		if (!sigismember(&sigset, SIGUSR1)) {
			pr_fail_err(name, "sigismember");
			continue;
		}

		/* Unmask signal, signal is handled */
		sigemptyset(&sigset);
		sigprocmask(SIG_SETMASK, &sigset, NULL);

		/* And it is no longer pending */
		if (sigpending(&sigset) < 0) {
			pr_fail_err(name, "sigpending");
			continue;
		}
		if (sigismember(&sigset, SIGUSR1)) {
			pr_fail_err(name, "sigismember");
			continue;
		}
		/* Success! */
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
