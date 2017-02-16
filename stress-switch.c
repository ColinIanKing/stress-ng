/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#define SWITCH_STOP	'X'

/*
 *  stress_switch
 *	stress by heavy context switching
 */
int stress_switch(const args_t *args)
{
	pid_t pid;
	int pipefds[2];
	size_t buf_size;

#if defined(__linux__) && NEED_GLIBC(2,9,0)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		pr_fail_dbg("pipe2");
		return EXIT_FAILURE;
	}
	buf_size = 1;
#else
	if (pipe(pipefds) < 0) {
		pr_fail_dbg("pipe");
		return EXIT_FAILURE;
	}
	buf_size = args->page_size;
#endif

#if defined(F_SETPIPE_SZ)
	if (fcntl(pipefds[0], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			args->name, errno, strerror(errno));
	}
	if (fcntl(pipefds[1], F_SETPIPE_SZ, buf_size) < 0) {
		pr_dbg("%s: could not force pipe size to 1 page, "
			"errno = %d (%s)\n",
			args->name, errno, strerror(errno));
	}
#endif

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		char buf[buf_size];

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		(void)close(pipefds[1]);

		while (g_keep_stressing_flag) {
			ssize_t ret;

			ret = read(pipefds[0], buf, sizeof(buf));
			if (ret < 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				pr_fail_dbg("read");
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
		(void)setpgid(pid, g_pgrp);
		(void)close(pipefds[0]);

		memset(buf, '_', buf_size);

		do {
			ssize_t ret;

			ret = write(pipefds[1], buf, sizeof(buf));
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail_dbg("write");
					break;
				}
				continue;
			}
			inc_counter(args);
		} while (keep_stressing());

		memset(buf, SWITCH_STOP, sizeof(buf));
		if (write(pipefds[1], buf, sizeof(buf)) <= 0)
			pr_fail_dbg("termination write");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	return EXIT_SUCCESS;
}
