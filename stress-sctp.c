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

#if defined(HAVE_LIB_SCTP)

#include <netinet/in.h>
#include <netinet/sctp.h>
#include <arpa/inet.h>
#if defined(AF_INET6)
#include <netinet/in.h>
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif

#if !defined(LOCALTIME_STREAM)
#define LOCALTIME_STREAM        0
#endif

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

#if defined(HAVE_LIB_SCTP)

/*
 *  stress_sctp_client()
 *	client reader
 */
static void stress_sctp_client(
	const args_t *args,
	const pid_t ppid)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
		struct sctp_event_subscribe events;
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}
		if ((fd = socket(opt_sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			opt_sctp_domain, opt_sctp_port,
			&addr, &addr_len, NET_ADDR_LOOPBACK);
		if (connect(fd, addr, addr_len) < 0) {
			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail_dbg("connect");
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
			pr_fail_dbg("setsockopt");
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
					pr_fail_dbg("recv");
				break;
			}
		} while (keep_stressing());
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

#if defined(AF_UNIX)
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
	g_keep_stressing_flag = false;
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int stress_sctp_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sighandler(args->name, SIGALRM, handle_sctp_sigalrm, NULL) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(opt_sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
		rc = exit_status(errno);
		pr_fail_dbg("socket");
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail_dbg("setsockopt");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	stress_set_sockaddr(args->name, args->instance, ppid,
		opt_sctp_domain, opt_sctp_port, &addr, &addr_len, NET_ADDR_ANY);
	if (bind(fd, addr, addr_len) < 0) {
		rc = exit_status(errno);
		pr_fail_dbg("bind");
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail_dbg("listen");
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
					pr_inf("%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#endif

			memset(buf, 'A' + (*args->counter % 26), sizeof(buf));

			for (i = 16; i < sizeof(buf); i += 16) {
				ssize_t ret = sctp_sendmsg(sfd, buf, i,
						NULL, 0, 0, 0,
						LOCALTIME_STREAM, 0, 0);
				if (ret < 0) {
					if (errno != EINTR)
						pr_fail_dbg("send");
					break;
				} else
					msgs++;
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX)
	if (addr && opt_sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	return rc;
}

/*
 *  stress_sctp
 *	stress SCTP by heavy SCTP network I/O
 */
int stress_sctp(const args_t *args)
{
	pid_t pid, ppid = getppid();

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, args->pid, opt_sctp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sctp_client(args, ppid);
		exit(EXIT_SUCCESS);
	} else {
		return stress_sctp_server(args, pid, ppid);
	}
}
#else
int stress_sctp(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
