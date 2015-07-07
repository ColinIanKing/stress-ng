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
 *  socket_pair_memset()
 *	set data to be incrementing chars from val upwards
 */
static inline void socket_pair_memset(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		*buf++ = val++;
}

/*
 *  socket_pair_memchk()
 *	check data contains incrementing chars from val upwards
 */
static inline int socket_pair_memchk(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		if (*buf++ != val++)
			return 1;
	return 0;
}

/*
 *  stress_socket_pair
 *	stress by heavy socket_pair I/O
 */
int stress_socket_pair(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int socket_pair_fds[2];

	(void)instance;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_fds) < 0) {
		pr_failed_dbg(name, "socket_pair");
		return EXIT_FAILURE;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		(void)close(socket_pair_fds[0]);
		(void)close(socket_pair_fds[1]);
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int val = 0;

		(void)close(socket_pair_fds[1]);
		for (;;) {
			char buf[SOCKET_PAIR_BUF];
			ssize_t n;

			n = read(socket_pair_fds[0], buf, sizeof(buf));
			if (n <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_failed_dbg(name, "read");
					break;
				}
				continue;
			}
			if (!strcmp(buf, PIPE_STOP))
				break;
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    socket_pair_memchk(buf, val++, (size_t)n)) {
				pr_fail(stderr, "%s: socket_pair read error detected, "
					"failed to read expected data\n", name);
			}
		}
		(void)close(socket_pair_fds[0]);
		exit(EXIT_SUCCESS);
	} else {
		char buf[SOCKET_PAIR_BUF];
		int val = 0, status;

		/* Parent */
		(void)close(socket_pair_fds[0]);

		do {
			ssize_t ret;
			size_t sz = (mwc16() % sizeof(buf)) + 1;

			socket_pair_memset(buf, val++, sz);
			ret = write(socket_pair_fds[1], buf, sz);
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_failed_dbg(name, "write");
					break;
				}
				continue;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		strncpy(buf, PIPE_STOP, sizeof(buf));
		if (write(socket_pair_fds[1], buf, sizeof(buf)) <= 0)
			pr_failed_dbg(name, "termination write");
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	return EXIT_SUCCESS;
}
