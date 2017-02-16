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

#if defined(__linux__) && NEED_GLIBC(2,8,0)

#include <sys/eventfd.h>

/*
 *  stress_eventfd
 *	stress eventfd read/writes
 */
int stress_eventfd(const args_t *args)
{
	pid_t pid;
	int fd1, fd2, rc;

	fd1 = eventfd(0, 0);
	if (fd1 < 0) {
		rc = exit_status(errno);
		pr_fail_dbg("eventfd");
		return rc;
	}
	fd2 = eventfd(0, 0);
	if (fd2 < 0) {
		rc = exit_status(errno);
		pr_fail_dbg("eventfd");
		(void)close(fd1);
		return rc;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		(void)close(fd1);
		(void)close(fd2);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		while (g_keep_stressing_flag) {
			uint64_t val;
			ssize_t ret;

			for (;;) {
				if (!g_keep_stressing_flag)
					goto exit_child;
				ret = read(fd1, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("child read");
					goto exit_child;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("child short read");
					goto exit_child;
				}
				break;
			}
			val = 1;

			for (;;) {
				if (!g_keep_stressing_flag)
					goto exit_child;
				ret = write(fd2, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("child write");
					goto exit_child;
				}
				if (ret  < (ssize_t)sizeof(val)) {
					pr_fail_dbg("child short write");
					goto exit_child;
				}
				break;
			}
		}
exit_child:
		(void)close(fd1);
		(void)close(fd2);
		exit(EXIT_SUCCESS);
	} else {
		int status;

		do {
			uint64_t val = 1;
			int ret;

			for (;;) {
				if (!g_keep_stressing_flag)
					goto exit_parent;

				ret = write(fd1, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("parent write");
					goto exit_parent;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("parent short write");
					goto exit_parent;
				}
				break;
			}

			for (;;) {
				if (!g_keep_stressing_flag)
					goto exit_parent;

				ret = read(fd2, &val, sizeof(val));
				if (ret < 0) {
					if ((errno == EAGAIN) ||
					    (errno == EINTR))
						continue;
					pr_fail_dbg("parent read");
					goto exit_parent;
				}
				if (ret < (ssize_t)sizeof(val)) {
					pr_fail_dbg("parent short read");
					goto exit_parent;
				}
				break;
			}
			inc_counter(args);
		} while (keep_stressing());
exit_parent:
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		(void)close(fd1);
		(void)close(fd2);
	}
	return EXIT_SUCCESS;
}
#else
int stress_eventfd(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
