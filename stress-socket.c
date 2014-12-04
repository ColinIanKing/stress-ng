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
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef AF_INET6
#include <netinet/in.h>
#endif

#include "stress-ng.h"

static int opt_domain = AF_INET;

typedef struct {
	const char *name;
	const int  domain;
} domain_t;

static const domain_t domains[] = {
	{ "ipv4",	AF_INET },
	{ "ipv6",	AF_INET6 },
	{ NULL,		-1 }
};

/*
 *  stress_set_socket_domain()
 *	set the socket domain option
 */
int stress_set_socket_domain(const char *name)
{
	int i;

	for (i = 0; domains[i].name; i++) {
		if (!strcmp(name, domains[i].name)) {
			opt_domain = domains[i].domain;
			return 0;
		}
	}
	fprintf(stderr, "socket domain must be one of:");
	for (i = 0; domains[i].name; i++)
		fprintf(stderr, " %s", domains[i].name);
	fprintf(stderr, "\n");
	return -1;
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

		do {
			char buf[SOCKET_BUF];
			int fd;
			int retries = 0;
			int ret = -1;
retry:
			if ((fd = socket(opt_domain, SOCK_STREAM, 0)) < 0) {
				pr_failed_dbg(name, "socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}

			switch (opt_domain) {
			case AF_INET: {
					struct sockaddr_in addr;

					memset(&addr, 0, sizeof(addr));
					addr.sin_family = opt_domain;
					addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
					addr.sin_port = htons(opt_socket_port + instance);
					ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
				}
				break;
#ifdef AF_INET6
			case AF_INET6: {
					struct sockaddr_in6 addr;

					memset(&addr, 0, sizeof(addr));
					addr.sin6_family = opt_domain;
					addr.sin6_addr = in6addr_loopback;
					addr.sin6_port = htons(opt_socket_port + instance);
					ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
				}
				break;
#endif
			default:
				pr_failed_dbg(name, "unknown domain");
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}

			if (ret < 0) {
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
					pr_failed_dbg(name, "recv");
					break;
				}
			} while (opt_do_run && (!max_ops || *counter < max_ops));
			(void)close(fd);
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */

		char buf[SOCKET_BUF];
		int fd, status, ret = -1;
		int so_reuseaddr = 1;
		struct sigaction new_action;

		new_action.sa_handler = handle_socket_sigalrm;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(SIGALRM, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(opt_domain, SOCK_STREAM, 0)) < 0) {
			pr_failed_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_failed_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		switch (opt_domain) {
		case AF_INET: {
				struct sockaddr_in addr;

				memset(&addr, 0, sizeof(addr));
				addr.sin_family = opt_domain;
				addr.sin_addr.s_addr = htonl(INADDR_ANY);
				addr.sin_port = htons(opt_socket_port + instance);
				ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
			}
			break;
#ifdef AF_INET6
		case AF_INET6: {
				struct sockaddr_in6 addr;

				memset(&addr, 0, sizeof(addr));
				addr.sin6_family = opt_domain;
				addr.sin6_addr = in6addr_any;
				addr.sin6_port = htons(opt_socket_port + instance);
				ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
			}
			break;
#endif
		default:
			pr_failed_dbg(name, "unknown domain");
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}
		if (ret < 0) {
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
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		}
	}
	return rc;
}
