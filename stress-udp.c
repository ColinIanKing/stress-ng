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

/* See bugs section of udplite(7) */
#if !defined(SOL_UDPLITE)
#define SOL_UDPLITE		(136)
#endif
#if !defined(UDPLITE_SEND_CSCOV)
#define UDPLITE_SEND_CSCOV	(10)
#endif
#if !defined(UDPLITE_RECV_CSCOV)
#define UDPLITE_RECV_CSCOV	(11)
#endif

static int opt_udp_domain = AF_INET;
static int opt_udp_port = DEFAULT_SOCKET_PORT;

void stress_set_udp_port(const char *optarg)
{
	stress_set_net_port("udp-port", optarg,
		MIN_UDP_PORT, MAX_UDP_PORT - STRESS_PROCS_MAX,
		&opt_udp_port);
}

/*
 *  stress_set_udp_domain()
 *	set the udp domain option
 */
int stress_set_udp_domain(const char *name)
{
	return stress_set_net_domain(DOMAIN_ALL, "udp-domain", name, &opt_udp_domain);
}

/*
 *  handle_udp_sigalrm()
 *	catch SIGALRM
 */
static void MLOCKED handle_udp_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;
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
#if defined(OPT_UDP_LITE)
	int proto = (opt_flags & OPT_FLAGS_UDP_LITE) ?
		IPPROTO_UDPLITE : IPPROTO_UDP;
#else
	int proto = 0;
#endif

#if defined(OPT_UDP_LITE)
	if ((proto == IPPROTO_UDPLITE) &&
	    (opt_udp_domain == AF_UNIX)) {
		proto = 0;
		if (instance == 0) {
			pr_inf(stderr, "%s: disabling UDP-Lite as it is not "
				"available for UNIX domain UDP\n",
				name);
		}
	}
#endif

	pr_dbg(stderr, "%s: process [%d] using udp port %d\n",
		name, getpid(), opt_udp_port + instance);

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr = NULL;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		do {
			char buf[UDP_BUF];
			socklen_t len;
			int fd;
			int j = 0;
#if defined(OPT_UDP_LITE)
			int val;
#endif

			if ((fd = socket(opt_udp_domain, SOCK_DGRAM, proto)) < 0) {
				pr_fail_dbg(name, "socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}
			stress_set_sockaddr(name, instance, ppid,
				opt_udp_domain, opt_udp_port, &addr, &len);
#if defined(OPT_UDP_LITE)
			if (proto == IPPROTO_UDPLITE) {
				val = 8;	/* Just the 8 byte header */
				if (setsockopt(fd, SOL_UDPLITE, UDPLITE_SEND_CSCOV, &val, sizeof(int)) < 0) {
					pr_fail_dbg(name, "setsockopt");
					(void)close(fd);
					(void)kill(getppid(), SIGALRM);
					exit(EXIT_FAILURE);
				}
			}
#endif
			do {
				size_t i;

				for (i = 16; i < sizeof(buf); i += 16, j++) {
					memset(buf, 'A' + (j % 26), sizeof(buf));
					ssize_t ret = sendto(fd, buf, i, 0, addr, len);
					if (ret < 0) {
						if (errno != EINTR)
							pr_fail_dbg(name, "sendto");
						break;
					}
				}
			} while (opt_do_run && (!max_ops || *counter < max_ops));
			(void)close(fd);
		} while (opt_do_run && (!max_ops || *counter < max_ops));

#ifdef AF_UNIX
		if ((opt_udp_domain == AF_UNIX) && addr) {
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
#if defined(OPT_UDP_LITE)
		int val;
#endif
		socklen_t addr_len = 0;
		struct sockaddr *addr = NULL;

		setpgid(pid, pgrp);

		if (stress_sighandler(name, SIGALRM, handle_udp_sigalrm, NULL) < 0) {
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(opt_udp_domain, SOCK_DGRAM, proto)) < 0) {
			pr_fail_dbg(name, "socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		stress_set_sockaddr(name, instance, ppid,
			opt_udp_domain, opt_udp_port, &addr, &addr_len);
#if defined(OPT_UDP_LITE)
		if (proto == IPPROTO_UDPLITE) {
			val = 8;	/* Just the 8 byte header */
			if (setsockopt(fd, SOL_UDPLITE, UDPLITE_RECV_CSCOV, &val, sizeof(int)) < 0) {
				pr_fail_dbg(name, "setsockopt");
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif
		if (bind(fd, addr, addr_len) < 0) {
			pr_fail_dbg(name, "bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_fail_dbg(name, "setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}

		do {
			socklen_t len = addr_len;
			ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail_dbg(name, "recvfrom");
				break;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

die_close:
		(void)close(fd);
die:
#ifdef AF_UNIX
		if ((opt_udp_domain == AF_UNIX) && addr) {
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
