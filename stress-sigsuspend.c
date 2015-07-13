/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include "stress-ng.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>

#define MAX_PIDS	(4)

/*
 *  stress_sigsuspend
 *	stress sigsuspend
 */
int stress_sigsuspend(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid[MAX_PIDS];
	size_t n, i;
	sigset_t mask;
	int status;

	(void)instance;

	sigfillset(&mask);
	sigdelset(&mask, SIGUSR1);

	for (n = 0; n < MAX_PIDS; n++) {
again:
		pid[n] = fork();
		if (pid[n] < 0) {
			if (opt_do_run && (errno == EAGAIN))
				goto again;
			pr_failed_dbg(name, "fork");
			goto reap;
		} else if (pid[n] == 0) {
			for (;;) {
				sigsuspend(&mask);
				pthread_spin_lock(&shared->sigsuspend.lock);
				(*counter)++;
				pthread_spin_unlock(&shared->sigsuspend.lock);
			}
			_exit(0);
		}
	}

	/* Parent */
	do {
		for (i = 0; i < n; i++)
			kill(pid[i], SIGUSR1);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

reap:
	for (i = 0; i < n; i++) {
		/* terminate child */
		(void)kill(pid[i], SIGKILL);
		(void)waitpid(pid[i], &status, 0);
	}

	return EXIT_SUCCESS;
}
