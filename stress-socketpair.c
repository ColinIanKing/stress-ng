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
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/socket.h>

#include "stress-ng.h"

#define MAX_SOCKET_PAIRS	(32768)

/*
 *  socket_pair_memset()
 *	set data to be incrementing chars from val upwards
 */
static inline void socket_pair_memset(
	uint8_t *buf,
	uint8_t val,
	const size_t sz)
{
	register uint8_t *ptr;
	register uint8_t checksum = 0;

	for (ptr = buf + 1 ; ptr < buf + sz; *ptr++ = val++)
		checksum += val;
	*buf = checksum;
}

/*
 *  socket_pair_memchk()
 *	check data contains incrementing chars from val upwards
 */
static inline int socket_pair_memchk(
	uint8_t *buf,
	const size_t sz)
{
	register uint8_t *ptr;
	register uint8_t checksum = 0;

	for (ptr = buf + 1; ptr < buf + sz; checksum += *ptr++)
		;

	return !(checksum == *buf);
}

static void socket_pair_close(
	int fds[MAX_SOCKET_PAIRS][2],
	const int max,
	const int which)
{
	int i;

	for (i = 0; i < max; i++)
		(void)close(fds[i][which]);

}

/*
 *  stress_sockpair
 *	stress by heavy socket_pair I/O
 */
int stress_sockpair(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int socket_pair_fds[MAX_SOCKET_PAIRS][2], i, max;

	(void)instance;

	for (max = 0; max < MAX_SOCKET_PAIRS; max++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_fds[max]) < 0)
			break;
	}

	if (max == 0) {
		pr_fail_dbg(name, "socket_pair");
		return EXIT_FAILURE;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		socket_pair_close(socket_pair_fds, max, 0);
		socket_pair_close(socket_pair_fds, max, 1);
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		socket_pair_close(socket_pair_fds, max, 1);
		while (opt_do_run) {
			uint8_t buf[SOCKET_PAIR_BUF];
			ssize_t n;

			for (i = 0; opt_do_run && (i < max); i++) {
				n = read(socket_pair_fds[i][0], buf, sizeof(buf));
				if (n <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno == ENFILE) /* Too many files! */
						goto abort;
					if (errno == EMFILE) /* Occurs on socket shutdown */
						goto abort;
					if (errno) {
						pr_fail_dbg(name, "read");
						break;
					}
					continue;
				}
				if ((opt_flags & OPT_FLAGS_VERIFY) &&
				    socket_pair_memchk(buf, (size_t)n)) {
					pr_fail(stderr, "%s: socket_pair read error detected, "
						"failed to read expected data\n", name);
				}
			}
		}
abort:
		socket_pair_close(socket_pair_fds, max, 0);
		exit(EXIT_SUCCESS);
	} else {
		uint8_t buf[SOCKET_PAIR_BUF];
		int val = 0, status;

		setpgid(pid, pgrp);
		/* Parent */
		socket_pair_close(socket_pair_fds, max, 0);
		do {
			for (i = 0; opt_do_run && (i < max); i++) {
				ssize_t ret;

				socket_pair_memset(buf, val++, sizeof(buf));
				ret = write(socket_pair_fds[i][1], buf, sizeof(buf));
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
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		for (i = 0; i < max; i++) {
			if (shutdown(socket_pair_fds[i][1], SHUT_RDWR) < 0)
				pr_fail_dbg(name, "socket shutdown");
		}
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		socket_pair_close(socket_pair_fds, max, 1);
	}
	return EXIT_SUCCESS;
}
