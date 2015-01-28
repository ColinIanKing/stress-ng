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

static int opt_udp_domain = AF_INET;
static int opt_udp_port = DEFAULT_SOCKET_PORT;

typedef struct {
	const char *name;
	const int  domain;
} domain_t;

static const domain_t domains[] = {
	{ "ipv4",	AF_INET },
	{ "ipv6",	AF_INET6 },
	{ "unix",	AF_UNIX },
	{ NULL,		-1 }
};

void stress_set_udp_port(const char *optarg)
{
	opt_udp_port = get_uint64(optarg);
	check_range("udp-port", opt_udp_port,
		MIN_UDP_PORT, MAX_UDP_PORT - STRESS_PROCS_MAX);
}

/*
 *  stress_set_udp_domain()
 *	set the udp domain option
 */
int stress_set_udp_domain(const char *name)
{
	int i;

	for (i = 0; domains[i].name; i++) {
		if (!strcmp(name, domains[i].name)) {
			opt_udp_domain = domains[i].domain;
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
 *  handle_udp_sigalrm()
 *	catch SIGALRM
 */
static void handle_udp_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;
}

/*
 *  setup socket address
 */
static void set_sockaddr(
	const char *name,
	const uint32_t instance,
	const pid_t ppid,
	struct sockaddr **sockaddr,
	socklen_t *len)
{
	switch (opt_udp_domain) {
#ifdef AF_INET
	case AF_INET: {
		static struct sockaddr_in addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = opt_udp_domain;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(opt_udp_port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#ifdef AF_INET6
	case AF_INET6: {
		static struct sockaddr_in6 addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin6_family = opt_udp_domain;
		addr.sin6_addr = in6addr_any;
		addr.sin6_port = htons(opt_udp_port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#ifdef AF_UNIX
	case AF_UNIX: {
		static struct sockaddr_un addr;

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		snprintf(addr.sun_path, sizeof(addr.sun_path),
			"/tmp/stress-ng-%d-%" PRIu32,
			ppid, instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
	default:
		pr_failed_dbg(name, "unknown domain");
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_FAILURE);
	}
}

/*
 *  stress_udp
 *	stress by heavy udp ops
 */
int stress_udp(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid, ppid = getppid();
	int rc = EXIT_SUCCESS;

	pr_dbg(stderr, "%s: process [%d] using udp port %d\n",
		name, getpid(), opt_udp_port + instance);

	pid = fork();
	if (pid < 0) {
		pr_failed_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr;

		do {
			char buf[UDP_BUF];
			socklen_t len;
			int fd;
			int j = 0;

			if ((fd = socket(opt_udp_domain, SOCK_DGRAM, 0)) < 0) {
				pr_failed_dbg(name, "socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}
			set_sockaddr(name, instance, ppid, &addr, &len);

			do {
				size_t i;

				for (i = 16; i < sizeof(buf); i += 16, j++) {
					memset(buf, 'A' + (j % 26), sizeof(buf));
					ssize_t ret = sendto(fd, buf, i, 0, addr, len);
					if (ret < 0) {
						pr_failed_dbg(name, "sendto");
						break;
					}
				}
			} while (opt_do_run && (!max_ops || *counter < max_ops));
			(void)close(fd);
		} while (opt_do_run && (!max_ops || *counter < max_ops));

#ifdef AF_UNIX
		if (opt_udp_domain == AF_UNIX) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
			(void)unlink(addr_un->sun_path);
		}
#endif
		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */

		char buf[UDP_BUF];
		int fd, status;
		int so_reuseaddr = 1;
		socklen_t addr_len = 0;
		struct sigaction new_action;
		struct sockaddr *addr;

		new_action.sa_handler = handle_udp_sigalrm;
		sigemptyset(&new_action.sa_mask);
		new_action.sa_flags = 0;
		if (sigaction(SIGALRM, &new_action, NULL) < 0) {
			pr_failed_err(name, "sigaction");
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(opt_udp_domain, SOCK_DGRAM, 0)) < 0) {
			pr_failed_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		set_sockaddr(name, instance, ppid, &addr, &addr_len);
		if (bind(fd, addr, addr_len) < 0) {
			pr_failed_dbg(name, "bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_failed_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		do {
			socklen_t len = addr_len;
			ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
			if (n == 0)
				break;
			if (n < 0) {
				pr_failed_dbg(name, "recvfrom");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

die_close:
		(void)close(fd);
die:
#ifdef AF_UNIX
		if (opt_udp_domain == AF_UNIX) {
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
