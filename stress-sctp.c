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

#include "stress-ng.h"

#if defined(STRESS_SCTP)

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
#include <netinet/sctp.h>
#include <arpa/inet.h>
#ifdef AF_INET6
#include <netinet/in.h>
#endif
#ifdef AF_UNIX
#include <sys/un.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

#if !defined(LOCALTIME_STREAM)
#define LOCALTIME_STREAM        0
#endif

static int opt_sctp_domain = AF_INET;
static int opt_sctp_port = DEFAULT_SCTP_PORT;

/*
 *  stress_set_sctp_port()
 *	set port to use
 */
void stress_set_sctp_port(const char *optarg)
{
	stress_set_net_port("sctp-port", optarg,
		MIN_SCTP_PORT, MAX_SCTP_PORT - STRESS_PROCS_MAX,
		&opt_sctp_port);
}

/*
 *  stress_set_sctp_domain()
 *	set the socket domain option
 */
int stress_set_sctp_domain(const char *name)
{
	return stress_set_net_domain(DOMAIN_ALL, "sctp-domain",
				     name, &opt_sctp_domain);
}

/*
 *  stress_sctp_client()
 *	client reader
 */
static void stress_sctp_client(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name,
	const pid_t ppid)
{
	struct sockaddr *addr;

	setpgid(0, pgrp);
	stress_parent_died_alarm();

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
		struct sctp_event_subscribe events;
retry:
		if (!opt_do_run) {
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}
		if ((fd = socket(opt_sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
			pr_fail_dbg(name, "socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(name, instance, ppid,
			opt_sctp_domain, opt_sctp_port,
			&addr, &addr_len, NET_ADDR_LOOPBACK);
		if (connect(fd, addr, addr_len) < 0) {
			(void)close(fd);
			usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail_dbg(name, "connect");
				(void)kill(getppid(), SIGALRM);
				exit(EXIT_FAILURE);
			}
			goto retry;
		}
		memset( &events, 0, sizeof(events));
		events.sctp_data_io_event = 1;
		if (setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events,
			sizeof(events)) < 0) {
			(void)close(fd);
			pr_fail_dbg(name, "setsockopt");
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}

		do {
			int flags;
			struct sctp_sndrcvinfo sndrcvinfo;

			ssize_t n = sctp_recvmsg(fd, buf, sizeof(buf),
				NULL, 0, &sndrcvinfo, &flags);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail_dbg(name, "recv");
				break;
			}
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

#ifdef AF_UNIX
	if (opt_sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  handle_sctp_sigalrm()
 *     catch SIGALRM
 *  stress_sctp_client()
 *     client reader
 */
static void MLOCKED handle_sctp_sigalrm(int dummy)
{
	(void)dummy;
	opt_do_run = false;
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int stress_sctp_server(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name,
	const pid_t pid,
	const pid_t ppid)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	setpgid(pid, pgrp);

	if (stress_sighandler(name, SIGALRM, handle_sctp_sigalrm, NULL) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(opt_sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
		rc = exit_status(errno);
		pr_fail_dbg(name, "socket");
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail_dbg(name, "setsockopt");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	stress_set_sockaddr(name, instance, ppid,
		opt_sctp_domain, opt_sctp_port, &addr, &addr_len, NET_ADDR_ANY);
	if (bind(fd, addr, addr_len) < 0) {
		rc = exit_status(errno);
		pr_fail_dbg(name, "bind");
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail_dbg(name, "listen");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	do {
		int sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			size_t i;
#if defined(SOCKET_NODELAY)
			int one = 1;

			if (opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
				if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
					pr_inf(stderr, "%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						name, errno, strerror(errno));
					opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#endif

			memset(buf, 'A' + (*counter % 26), sizeof(buf));

			for (i = 16; i < sizeof(buf); i += 16) {
				ssize_t ret = sctp_sendmsg(sfd, buf, i,
						NULL, 0, 0, 0,
						LOCALTIME_STREAM, 0, 0);
				if (ret < 0) {
					if (errno != EINTR)
						pr_fail_dbg(name, "send");
					break;
				} else
					msgs++;
			}
			(void)close(sfd);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

die_close:
	(void)close(fd);
die:
#ifdef AF_UNIX
	if (opt_sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	pr_dbg(stderr, "%s: %" PRIu64 " messages sent\n", name, msgs);

	return rc;
}

/*
 *  stress_sctp
 *	stress SCTP by heavy SCTP network I/O
 */
int stress_sctp(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid, ppid = getppid();

	pr_dbg(stderr, "%s: process [%d] using socket port %d\n",
		name, getpid(), opt_sctp_port + instance);

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sctp_client(counter, instance, max_ops, name, ppid);
		exit(EXIT_SUCCESS);
	} else {
		return stress_sctp_server(counter, instance, max_ops, name, pid, ppid);
	}
}

#endif
