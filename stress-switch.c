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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

#define SWITCH_STOP	'X'

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
	size_t buf_size;

	(void)instance;

#if defined(__linux__) && NEED_GLIBC(2,9,0)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		pr_fail_dbg(name, "pipe2");
		return EXIT_FAILURE;
	}
	buf_size = 1;
#else
	if (pipe(pipefds) < 0) {
		pr_fail_dbg(name, "pipe");
		return EXIT_FAILURE;
	}
	buf_size = stress_get_pagesize();
#endif

#if defined(F_SETPIPE_SZ)
	if (fcntl(pipefds[0], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg(stderr, "%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			name, errno, strerror(errno));
	}
	if (fcntl(pipefds[1], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg(stderr, "%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			name, errno, strerror(errno));
	}
#endif

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		char buf[buf_size];

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		(void)close(pipefds[1]);

		while (opt_do_run) {
			ssize_t ret;

			ret = read(pipefds[0], buf, sizeof(buf));
			if (ret < 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail_dbg(name, "read");
				break;
			}
			if (ret == 0)
				break;
			if (*buf == SWITCH_STOP)
				break;
		}
		(void)close(pipefds[0]);
		exit(EXIT_SUCCESS);
	} else {
		char buf[buf_size];
		int status;

		/* Parent */
		setpgid(pid, pgrp);
		(void)close(pipefds[0]);

		memset(buf, '_', buf_size);

		do {
			ssize_t ret;

			ret = write(pipefds[1], buf, sizeof(buf));
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail_dbg(name, "write");
					break;
				}
				continue;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		memset(buf, SWITCH_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_fail_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
