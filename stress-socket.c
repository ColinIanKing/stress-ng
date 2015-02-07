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
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef AF_INET6
#include <netinet/in.h>
#endif
#ifdef AF_UNIX
#include <sys/un.h>
#endif

#include "stress-ng.h"

static int opt_socket_domain = AF_INET;
static int opt_socket_port = DEFAULT_SOCKET_PORT;

void stress_set_socket_port(const char *optarg)
{
	stress_set_net_port("sock-port", optarg,
		MIN_SOCKET_PORT, MAX_SOCKET_PORT - STRESS_PROCS_MAX,
		&opt_socket_port);
}

/*
 *  stress_set_socket_domain()
 *	set the socket domain option
 */
int stress_set_socket_domain(const char *name)
{
	return stress_set_net_domain("sock-domain", name, &opt_socket_domain);
}

/*
 *  handle_socket_sigalrm()
 *	catch SIGALRM
 */
static void handle_socket_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;
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
	pid_t pid, ppid = getppid();
	int rc = EXIT_SUCCESS;

	pr_dbg(stderr, "%s: process [%d] using socket port %d\n",
		name, getpid(), opt_socket_port + instance);

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr;

		do {
			char buf[SOCKET_BUF];
			int fd;
			int retries = 0;
			socklen_t addr_len = 0;
retry:
			if ((fd = socket(opt_socket_domain, SOCK_STREAM, 0)) < 0) {
				pr_failed_dbg(name, "socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}

			stress_set_sockaddr(name, instance, ppid,
				opt_socket_domain, opt_socket_port, &addr, &addr_len);
			if (connect(fd, addr, addr_len) < 0) {
				(void)close(fd);
				usleep(10000);
				retries++;
				if (retries > 100) {
					/* Give up.. */
					pr_failed_dbg(name, "connect");
					(void)kill(getppid(), SIGALRM);
					exit(EXIT_FAILURE);
				}
				goto retry;
			}

			retries = 0;
			do {
				ssize_t n = recv(fd, buf, sizeof(buf), 0);
				if (n == 0)
					break;
				if (n < 0) {
					if (errno != EINTR)
						pr_failed_dbg(name, "recv");
					break;
				}
			} while (opt_do_run && (!max_ops || *counter < max_ops));
			(void)close(fd);
		} while (opt_do_run && (!max_ops || *counter < max_ops));

#ifdef AF_UNIX
		if (opt_socket_domain == AF_UNIX) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
			(void)unlink(addr_un->sun_path);
		}
#endif
		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */

		char buf[SOCKET_BUF];
		int fd, status;
		int so_reuseaddr = 1;
		struct sigaction new_action;
		socklen_t addr_len = 0;
		struct sockaddr *addr;

		new_action.sa_handler = handle_socket_sigalrm;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(SIGALRM, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(opt_socket_domain, SOCK_STREAM, 0)) < 0) {
			pr_failed_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_failed_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		stress_set_sockaddr(name, instance, ppid,
			opt_socket_domain, opt_socket_port, &addr, &addr_len);
		if (bind(fd, addr, addr_len) < 0) {
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
					ssize_t ret = send(sfd, buf, i, 0);
					if (ret < 0) {
						if (errno != EINTR)
							pr_failed_dbg(name, "send");
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
#ifdef AF_UNIX
		if (opt_socket_domain == AF_UNIX) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
			(void)unlink(addr_un->sun_path);
		}
#endif
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		}
	}
	return rc;
}
