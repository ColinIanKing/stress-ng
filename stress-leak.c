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
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/resource.h>

#define MMAP_SIZE	(1024 * 1024)

#include "stress-ng.h"

/*
 *  stress_leak()
 *	stress by leaking and exiting
 */
int stress_leak(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	struct rlimit rlim;
	rlim_t i, opened = 0;
	pid_t pid;
	int pipefds[2];

	(void)instance;

	if (pipe(pipefds) < 0) {
		pr_fail_dbg(name, "pipe");
		return EXIT_FAILURE;
	}

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
		rlim.rlim_cur = STRESS_FD_MAX;  /* Guess */
	for (i = 0; i < rlim.rlim_cur; i++) {
		if (fcntl((int)i, F_GETFL) > -1)
			opened++;
	}

	rlim.rlim_cur -= opened;

	do {
		pid = fork();

		if (pid == 0) {
			/* Child */

			int fds[STRESS_FD_MAX];
			void *mem;

			setpgid(0, pgrp);
			stress_parent_died_alarm();

			mem = mmap(NULL, MMAP_SIZE, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
			if (mem != MAP_FAILED)
				memset(mem, 0, MMAP_SIZE);
			for (opened = 0, i = 0; i < rlim.rlim_cur; i++) {
				fds[i] = open("/dev/zero", O_RDONLY);
				if (fds[i] >= 0)
					opened++;
				else
					break;
			}
			_exit(0);
		} else if (pid > -1) {
			/* Parent, wait for child */
			int status;

			setpgid(pid, pgrp);
			if (!opt_do_run)
				break;

			(void)waitpid(pid, &status, 0);
			(*counter)++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(pipefds[0]);
	(void)close(pipefds[1]);

	return EXIT_SUCCESS;
}
