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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

/*
 *  stress_switch
 *	stress by heavy context switching
 */
int stress_switch(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int pipefds[2];

	(void)instance;

	if (pipe(pipefds) < 0) {
		pr_failed_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)close(pipefds[1]);

		for (;;) {
			char ch;

			for (;;) {
				if (read(pipefds[0], &ch, sizeof(ch)) <= 0) {
					pr_failed_dbg(name, "read");
					break;
				}
				if (ch == SWITCH_STOP)
					break;
			}
			(void)close(pipefds[0]);
			exit(EXIT_SUCCESS);
		}
	} else {
		char ch = '_';
		int status;

		/* Parent */
		(void)close(pipefds[0]);

		do {
			if (write(pipefds[1],  &ch, sizeof(ch)) < 0) {
				pr_failed_dbg(name, "write");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		ch = SWITCH_STOP;
		if (write(pipefds[1],  &ch, sizeof(ch)) <= 0)
			pr_failed_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
