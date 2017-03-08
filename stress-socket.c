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

#if defined(__linux__)
#define SOCKET_NODELAY
#endif

#if defined(SOCKET_NODELAY)
#include <netinet/tcp.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(AF_INET6)
#include <netinet/in.h>
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif

#if defined(__linux__) && defined(__NR_sendmmsg) && NEED_GLIBC(2,14,0)
#define HAVE_SENDMMSG
#endif

#define SOCKET_OPT_SEND		0x01
#define SOCKET_OPT_SENDMSG	0x02
#define SOCKET_OPT_SENDMMSG	0x03

#define MSGVEC_SIZE		(4)

typedef struct {
	const char *optname;
	int	   opt;
} socket_opts_t;

typedef struct {
	const char *typename;
	int	   type;
} socket_type_t;

static int opt_socket_domain = AF_INET;
static int opt_socket_port = DEFAULT_SOCKET_PORT;
static int opt_socket_opts = SOCKET_OPT_SEND;
static int opt_socket_type = SOCK_STREAM;

static const socket_opts_t socket_opts[] = {
	{ "send",	SOCKET_OPT_SEND },
	{ "sendmsg",	SOCKET_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
	{ "sendmmsg",	SOCKET_OPT_SENDMMSG },
#endif
	{ NULL,		0 }
};

static const socket_type_t socket_type[] = {
#if defined(SOCK_STREAM)
	{ "stream",	SOCK_STREAM  },
#endif
#if defined(SOCK_SEQPACKET)
	{ "seqpacket",	SOCK_SEQPACKET },
#endif
	{ NULL,		0 }
};

/*
 *  stress_set_socket_opts()
 *	parse --sock-opts
 */
int stress_set_socket_opts(const char *optarg)
{
	int i;

	for (i = 0; socket_opts[i].optname; i++) {
		if (!strcmp(optarg, socket_opts[i].optname)) {
			opt_socket_opts = socket_opts[i].opt;
			return 0;
		}
	}
	(void)fprintf(stderr, "sock-opts option '%s' not known, options are:", optarg);
	for (i = 0; socket_opts[i].optname; i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", socket_opts[i].optname);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_socket_type()
 *	parse --sock-type
 */
int stress_set_socket_type(const char *optarg)
{
	int i;

	for (i = 0; socket_type[i].typename; i++) {
		if (!strcmp(optarg, socket_type[i].typename)) {
			opt_socket_type = socket_type[i].type;
			return 0;
		}
	}
	(void)fprintf(stderr, "sock-type option '%s' not known, options are:", optarg);
	for (i = 0; socket_type[i].typename; i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", socket_type[i].typename);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_socket_port()
 *	set port to use
 */
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
	return stress_set_net_domain(DOMAIN_ALL, "sock-domain",
				     name, &opt_socket_domain);
}

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
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}
		if ((fd = socket(opt_socket_domain, opt_socket_type, 0)) < 0) {
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			opt_socket_domain, opt_socket_port,
			&addr, &addr_len, NET_ADDR_ANY);
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

		do {
			ssize_t n = recv(fd, buf, sizeof(buf), 0);
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
	if (opt_socket_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  handle_socket_sigalrm()
 *     catch SIGALRM
 *  stress_sctp_client()
 *     client reader
 */
static void MLOCKED handle_socket_sigalrm(int dummy)
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

	if (stress_sighandler(args->name, SIGALRM, handle_socket_sigalrm, NULL) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(opt_socket_domain, opt_socket_type, 0)) < 0) {
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
		opt_socket_domain, opt_socket_port,
		&addr, &addr_len, NET_ADDR_ANY);
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
			size_t i, j;
			struct sockaddr saddr;
			socklen_t len;
			int sndbuf;
			struct msghdr msg;
			struct iovec vec[sizeof(buf)/16];
#if defined(HAVE_SENDMMSG)
			struct mmsghdr msgvec[MSGVEC_SIZE];
			unsigned int msg_len = 0;
#endif
#if defined(SOCKET_NODELAY)
			int one = 1;
#endif
			len = sizeof(saddr);
			if (getsockname(fd, &saddr, &len) < 0) {
				pr_fail_dbg("getsockname");
				(void)close(sfd);
				break;
			}
			len = sizeof(sndbuf);
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0) {
				pr_fail_dbg("getsockopt");
				(void)close(sfd);
				break;
			}

#if defined(SOCKET_NODELAY)
			if (g_opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
				if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
					pr_inf("%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					g_opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#endif
			memset(buf, 'A' + (*args->counter % 26), sizeof(buf));
			switch (opt_socket_opts) {
			case SOCKET_OPT_SEND:
				for (i = 16; i < sizeof(buf); i += 16) {
					ssize_t ret = send(sfd, buf, i, 0);
					if (ret < 0) {
						if (errno != EINTR)
							pr_fail_dbg("send");
						break;
					} else
						msgs++;
				}
				break;
			case SOCKET_OPT_SENDMSG:
				for (j = 0, i = 16; i < sizeof(buf); i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				if (sendmsg(sfd, &msg, 0) < 0) {
					if (errno != EINTR)
						pr_fail_dbg("sendmsg");
				} else
					msgs += j;
				break;
#if defined(HAVE_SENDMMSG)
			case SOCKET_OPT_SENDMMSG:
				memset(msgvec, 0, sizeof(msgvec));
				for (j = 0, i = 16; i < sizeof(buf); i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
					msg_len += i;
				}
				for (i = 0; i < MSGVEC_SIZE; i++) {
					msgvec[i].msg_hdr.msg_iov = vec;
					msgvec[i].msg_hdr.msg_iovlen = j;
				}
				if (sendmmsg(sfd, msgvec, MSGVEC_SIZE, 0) < 0) {
					if (errno != EINTR)
						pr_fail_dbg("sendmmsg");
				} else
					msgs += (MSGVEC_SIZE * j);
				break;
#endif
			default:
				/* Should never happen */
				pr_err("%s: bad option %d\n", args->name, opt_socket_opts);
				(void)close(sfd);
				goto die_close;
			}
			if (getpeername(sfd, &saddr, &len) < 0) {
				pr_fail_dbg("getpeername");
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX)
	if (addr && (opt_socket_domain == AF_UNIX)) {
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
 *  stress_sock
 *	stress by heavy socket I/O
 */
int stress_sock(const args_t *args)
{
	pid_t pid, ppid = getppid();

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, opt_socket_port + args->instance);

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
