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

#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(AF_INET6)
#include <netinet/in.h>
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif

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
	g_keep_stressing_flag = false;
}

/*
 *  stress_udp
 *	stress by heavy udp ops
 */
int stress_udp(const args_t *args)
{
	pid_t pid, ppid = getppid();
	int rc = EXIT_SUCCESS;
#if defined(IPPROTO_UDPLITE)
	int proto = (g_opt_flags & OPT_FLAGS_UDP_LITE) ?
		IPPROTO_UDPLITE : IPPROTO_UDP;
#else
	int proto = 0;
#endif

#if defined(IPPROTO_UDPLITE)
	if ((proto == IPPROTO_UDPLITE) &&
	    (opt_udp_domain == AF_UNIX)) {
		proto = 0;
		if (args->instance == 0) {
			pr_inf("%s: disabling UDP-Lite as it is not "
				"available for UNIX domain UDP\n",
				args->name);
		}
	}
#endif

	pr_dbg("%s: process [%d] using udp port %d\n",
		args->name, (int)args->pid, opt_udp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr = NULL;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		do {
			char buf[UDP_BUF];
			socklen_t len;
			int fd;
			int j = 0;
#if defined(IPPROTO_UDPLITE)
			int val;
#endif

			if ((fd = socket(opt_udp_domain, SOCK_DGRAM, proto)) < 0) {
				pr_fail_dbg("socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}
			stress_set_sockaddr(args->name, args->instance, ppid,
				opt_udp_domain, opt_udp_port,
				&addr, &len, NET_ADDR_ANY);
#if defined(IPPROTO_UDPLITE)
			if (proto == IPPROTO_UDPLITE) {
				val = 8;	/* Just the 8 byte header */
				if (setsockopt(fd, SOL_UDPLITE, UDPLITE_SEND_CSCOV, &val, sizeof(int)) < 0) {
					pr_fail_dbg("setsockopt");
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
							pr_fail_dbg("sendto");
						break;
					}
				}
			} while (keep_stressing());
			(void)close(fd);
		} while (keep_stressing());

#if defined(AF_UNIX)
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
#if !defined(__minix__)
		int so_reuseaddr = 1;
#endif
#if defined(IPPROTO_UDPLITE)
		int val;
#endif
		socklen_t addr_len = 0;
		struct sockaddr *addr = NULL;

		(void)setpgid(pid, g_pgrp);

		if (stress_sighandler(args->name, SIGALRM, handle_udp_sigalrm, NULL) < 0) {
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(opt_udp_domain, SOCK_DGRAM, proto)) < 0) {
			pr_fail_dbg("socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		stress_set_sockaddr(args->name, args->instance, ppid,
			opt_udp_domain, opt_udp_port,
			&addr, &addr_len, NET_ADDR_ANY);
#if defined(IPPROTO_UDPLITE)
		if (proto == IPPROTO_UDPLITE) {
			val = 8;	/* Just the 8 byte header */
			if (setsockopt(fd, SOL_UDPLITE, UDPLITE_RECV_CSCOV, &val, sizeof(int)) < 0) {
				pr_fail_dbg("setsockopt");
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif
		if (bind(fd, addr, addr_len) < 0) {
			pr_fail_dbg("bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
#if !defined(__minix__)
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			pr_fail_dbg("setsockopt");
			rc = EXIT_FAILURE;
			goto die_close;
		}
#endif

		do {
			socklen_t len = addr_len;
			ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail_dbg("recvfrom");
				break;
			}
			inc_counter(args);
		} while (keep_stressing());

die_close:
		(void)close(fd);
die:
#if defined(AF_UNIX)
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
