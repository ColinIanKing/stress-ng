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

#if defined (__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/eventfd.h>

#include "stress-ng.h"

/*
 *  stress_eventfd
 *	stress eventfd read/writes
 */
int stress_eventfd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int fd1, fd2;

	(void)instance;

	fd1 = eventfd(0, 0);
	if (fd1 < 0) {
		pr_failed_dbg(name, "eventfd");
		return EXIT_FAILURE;
	}
	fd2 = eventfd(0, 0);
	if (fd1 < 0) {
		pr_failed_dbg(name, "eventfd");
		(void)close(fd1);
		return EXIT_FAILURE;
	}

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		(void)close(fd1);
		(void)close(fd2);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		for (;;) {
			uint64_t val;

			if (read(fd1, &val, sizeof(val)) < (ssize_t)sizeof(val)) {
				pr_failed_dbg(name, "child read");
				break;
			}
			val = 1;
			if (write(fd2, &val, sizeof(val)) < (ssize_t)sizeof(val)) {
				pr_failed_dbg(name, "child write");
				break;
			}
		}
		(void)close(fd1);
		(void)close(fd2);
		exit(EXIT_SUCCESS);
	} else {
		int status;

		do {
			uint64_t val = 1;

			if (write(fd1, &val, sizeof(val)) < (ssize_t)sizeof(val)) {
				if (errno != EINTR)
					pr_failed_dbg(name, "parent write");
				break;
			}
			if (read(fd2, &val, sizeof(val)) < (ssize_t)sizeof(val)) {
				if (errno != EINTR)
					pr_failed_dbg(name, "parent read");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		(void)close(fd1);
		(void)close(fd2);
	}
	return EXIT_SUCCESS;
}

#endif
