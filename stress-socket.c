/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "stress-ng.h"

/*
 *  handle_socket_sigalrm()
 *	catch SIGALRM
 */
static void handle_socket_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;

	if (socket_client)
		(void)kill(socket_client, SIGKILL);
	if (socket_server)
		(void)kill(socket_server, SIGKILL);
}

/*
 *  stress_socket
 *	stress by heavy socket I/O
 */
int stress_socket(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int rc = EXIT_SUCCESS;

	pr_dbg(stderr, "%s: process [%d] using socket port %d\n",
		name, getpid(), opt_socket_port + instance);

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */

		for (;;) {
			char buf[SOCKET_BUF];
			ssize_t n;
			struct sockaddr_in addr;
			int fd;
			int retries = 0;
retry:
			if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				pr_failed_dbg(name, "socket");
				exit(EXIT_FAILURE);
			}

			memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			addr.sin_port = htons(opt_socket_port + instance);

			if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
				(void)close(fd);
				usleep(10000);
				retries++;
				if (retries > 100) {
					pr_failed_dbg(name, "connect");
					break;
				}
				goto retry;
			}

			retries = 0;
			for (;;) {
				n = read(fd, buf, sizeof(buf));
				if (n == 0)
					break;
				if (n < 0) {
					pr_failed_dbg(name, "write");
					break;
				}
			}
			(void)close(fd);
		}
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_FAILURE);
	} else {
		/* Parent, server */

		char buf[SOCKET_BUF];
		int fd, status;
		struct sockaddr_in addr;
		int so_reuseaddr = 1;
		struct sigaction new_action;

		socket_server = getpid();
		socket_client = pid;

		new_action.sa_handler = handle_socket_sigalrm;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(SIGALRM, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			pr_failed_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(opt_socket_port + instance);

		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_failed_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
			pr_failed_dbg(name, "bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (listen(fd, 10) < 0) {
			pr_failed_dbg(name, "listen");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		do {
			int sfd = accept(fd, (struct sockaddr *)NULL, NULL);
			if (sfd >= 0) {
				size_t i;

				memset(buf, 'A' + (*counter % 26), sizeof(buf));
				for (i = 16; i < sizeof(buf); i += 16) {
					int ret = write(sfd, buf, i);
					if (ret < 0) {
						pr_failed_dbg(name, "write");
						break;
					}
				}
				(void)close(sfd);
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

die_close:
		(void)close(fd);
die:
		(void)kill(pid, SIGKILL);
		waitpid(pid, &status, 0);
	}
	return rc;
}
