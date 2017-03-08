/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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

#if defined(__linux__) && defined(__NR_sendmmsg) && NEED_GLIBC(2,14,0)
#define HAVE_SENDMMSG
#endif

#define DCCP_OPT_SEND		0x01
#define DCCP_OPT_SENDMSG	0x02
#define DCCP_OPT_SENDMMSG	0x03

#define MSGVEC_SIZE		(4)

typedef struct {
	const char *optname;
	int	   opt;
} dccp_opts_t;

static int opt_dccp_domain = AF_INET;
static int opt_dccp_port = DEFAULT_DCCP_PORT;
static int opt_dccp_opts = DCCP_OPT_SEND;

static const dccp_opts_t dccp_opts[] = {
	{ "send",	DCCP_OPT_SEND },
	{ "sendmsg",	DCCP_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
	{ "sendmmsg",	DCCP_OPT_SENDMMSG },
#endif
	{ NULL,		0 }
};

/*
 *  stress_set_dccp_opts()
 *	parse --dccp-opts
 */
int stress_set_dccp_opts(const char *optarg)
{
	int i;

	for (i = 0; dccp_opts[i].optname; i++) {
		if (!strcmp(optarg, dccp_opts[i].optname)) {
			opt_dccp_opts = dccp_opts[i].opt;
			return 0;
		}
	}
	(void)fprintf(stderr, "dccp-opts option '%s' not known, options are:", optarg);
	for (i = 0; dccp_opts[i].optname; i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", dccp_opts[i].optname);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_dccp_port()
 *	set port to use
 */
void stress_set_dccp_port(const char *optarg)
{
	stress_set_net_port("dccp-port", optarg,
		MIN_DCCP_PORT, MAX_DCCP_PORT - STRESS_PROCS_MAX,
		&opt_dccp_port);
}

/*
 *  stress_set_dccp_domain()
 *	set the socket domain option
 */
int stress_set_dccp_domain(const char *name)
{
	return stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "dccp-domain",
				     name, &opt_dccp_domain);
}

#if defined(SOCK_DCCP) && defined(IPPROTO_DCCP)

/*
 *  stress_dccp_client()
 *	client reader
 */
static void stress_dccp_client(
	const args_t *args,
	const pid_t ppid)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[DCCP_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}
		if ((fd = socket(opt_dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			opt_dccp_domain, opt_dccp_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
			int err = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				errno = err;
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
	if (opt_dccp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  handle_dccp_sigalrm()
 *     catch SIGALRM
 *  stress_dccp_client()
 *     client reader
 */
static void MLOCKED handle_dccp_sigalrm(int dummy)
{
	(void)dummy;
	g_keep_stressing_flag = false;
}

/*
 *  stress_dccp_server()
 *	server writer
 */
static int stress_dccp_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid)
{
	char buf[DCCP_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sighandler(args->name, SIGALRM, handle_dccp_sigalrm, NULL) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(opt_dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
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
		opt_dccp_domain, opt_dccp_port,
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

			memset(buf, 'A' + (*args->counter % 26), sizeof(buf));
			switch (opt_dccp_opts) {
			case DCCP_OPT_SEND:
				for (i = 16; i < sizeof(buf); i += 16) {
					ssize_t ret;
again:
					ret = send(sfd, buf, i, 0);
					if (ret < 0) {
						if (errno == EAGAIN)
							goto again;
						if (errno != EINTR)
							pr_fail_dbg("send");
						break;
					} else
						msgs++;
				}
				break;
			case DCCP_OPT_SENDMSG:
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
			case DCCP_OPT_SENDMMSG:
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
				pr_err("%s: bad option %d\n", args->name, opt_dccp_opts);
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
	if (addr && (opt_dccp_domain == AF_UNIX)) {
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
 *  stress_dccp
 *	stress by heavy dccp  I/O
 */
int stress_dccp(const args_t *args)
{
	pid_t pid, ppid = getppid();

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, opt_dccp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_dccp_client(args, ppid);
		exit(EXIT_SUCCESS);
	} else {
		return stress_dccp_server(args, pid, ppid);
	}
}

#else

int stress_dccp(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
