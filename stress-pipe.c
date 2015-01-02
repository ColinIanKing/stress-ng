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
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

/*
 *  pipe_memset()
 *	set pipe data to be incrementing chars from val upwards
 */
static inline void pipe_memset(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		*buf++ = val++;
}

/*
 *  pipe_memchk()
 *	check pipe data contains incrementing chars from val upwards
 */
static inline int pipe_memchk(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		if (*buf++ != val++)
			return 1;
	return 0;
}

/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
int stress_pipe(
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
		int val = 0;

		(void)close(pipefds[1]);
		for (;;) {
			char buf[PIPE_BUF];
			ssize_t n;

			n = read(pipefds[0], buf, sizeof(buf));
			if (n <= 0) {
				pr_failed_dbg(name, "read");
				break;
			}
			if (!strcmp(buf, PIPE_STOP))
				break;
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    pipe_memchk(buf, val++, (size_t)n)) {
				pr_fail(stderr, "pipe read error detected, failed to read expected data\n");
			}
		}
		(void)close(pipefds[0]);
		exit(EXIT_SUCCESS);
	} else {
		char buf[PIPE_BUF];
		int val = 0, status;

		/* Parent */
		(void)close(pipefds[0]);

		do {
			pipe_memset(buf, val++, sizeof(buf));
			if (write(pipefds[1], buf, sizeof(buf)) < 0) {
				pr_failed_dbg(name, "write");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		strncpy(buf, PIPE_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_failed_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	}
	return EXIT_SUCCESS;
}
