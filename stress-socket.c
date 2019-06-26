/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
	const int   type;
} socket_type_t;

static const help_t help[] = {
	{ "S N", "sock N",		"start N workers exercising socket I/O" },
	{ NULL,	"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,	"sock-nodelay",		"disable Nagle algorithm, send data immediately" },
	{ NULL,	"sock-ops N",		"stop after N socket bogo operations" },
	{ NULL,	"sock-opts option", 	"socket options [send|sendmsg|sendmmsg]" },
	{ NULL,	"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL,	"sock-type T",		"socket type (stream, seqpacket)" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_socket_opts()
 *	parse --sock-opts
 */
static int stress_set_socket_opts(const char *opt)
{
	static const socket_opts_t socket_opts[] = {
		{ "send",	SOCKET_OPT_SEND },
		{ "sendmsg",	SOCKET_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
		{ "sendmmsg",	SOCKET_OPT_SENDMMSG },
#endif
		{ NULL,		0 }
	};

	int i;

	for (i = 0; socket_opts[i].optname; i++) {
		if (!strcmp(opt, socket_opts[i].optname)) {
			int opts = socket_opts[i].opt;

			set_setting("sock-opts", TYPE_ID_INT, &opts);
			return 0;
		}
	}
	(void)fprintf(stderr, "sock-opts option '%s' not known, options are:", opt);
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
static int stress_set_socket_type(const char *opt)
{
	static const socket_type_t socket_type[] = {
#if defined(SOCK_STREAM)
		{ "stream",	SOCK_STREAM  },
#endif
#if defined(SOCK_SEQPACKET)
		{ "seqpacket",	SOCK_SEQPACKET },
#endif
		{ NULL,		0 }
	};

	int i;

	for (i = 0; socket_type[i].typename; i++) {
		if (!strcmp(opt, socket_type[i].typename)) {
			int type = socket_type[i].type;

			set_setting("sock-type", TYPE_ID_INT, &type);
			return 0;
		}
	}
	(void)fprintf(stderr, "sock-type option '%s' not known, options are:", opt);
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
static int stress_set_socket_port(const char *opt)
{
	int socket_port;

	stress_set_net_port("sock-port", opt,
		MIN_SOCKET_PORT, MAX_SOCKET_PORT - STRESS_PROCS_MAX,
		&socket_port);
	return set_setting("sock-port", TYPE_ID_INT, &socket_port);
}

/*
 *  stress_set_socket_domain()
 *	set the socket domain option
 */
static int stress_set_socket_domain(const char *name)
{
	int ret, socket_domain;

	ret = stress_set_net_domain(DOMAIN_ALL, "sock-domain",
				     name, &socket_domain);
	set_setting("sock-domain", TYPE_ID_INT, &socket_domain);

	return ret;
}

/*
 *  stress_sctp_client()
 *	client reader
 */
static void stress_sctp_client(
	const args_t *args,
	const pid_t ppid,
	const int socket_type,
	const int socket_port,
	const int socket_domain)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
#if defined(FIONREAD)
		int count = 0;
#endif
		socklen_t addr_len = 0;
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(socket_domain, socket_type, 0)) < 0) {
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			socket_domain, socket_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail_dbg("connect");
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			goto retry;
		}

		do {
			ssize_t n;
#if defined(FIONREAD)
			size_t bytes = sizeof(buf);
			/*
			 *  Exercise FIONREAD ioctl. Linux supports
			 *  this also with SIOCINQ but lets try and
			 *  do the more standard way of peeking the
			 *  pending data size.  Do this infrequently
			 *  to ensure we exercise it without impacting
			 *  performance.
			 */
			if (count++ > 1024) {
				int ret;

				ret = ioctl(fd, FIONREAD, &bytes);
				(void)ret;
				count = 0;

				if (bytes > sizeof(buf))
					bytes = sizeof(buf);
			}
#endif
			n = recv(fd, buf, sizeof(buf), 0);
			if (n == 0)
				break;
			if (n < 0) {
				if ((errno != EINTR) && (errno != ECONNRESET))
					pr_fail_dbg("recv");
				break;
			}
		} while (keep_stressing());

#if defined(AF_INET) && 	\
    defined(IPPROTO_IP)	&&	\
    defined(IP_MTU)
		/* Exercise IP_MTU */
		if (socket_domain == AF_INET) {
			int ret, mtu;
			socklen_t mtu_len = sizeof(mtu);

			ret = getsockopt(fd, IPPROTO_IP, IP_MTU, &mtu, &mtu_len);
			(void)ret;
		}
#endif

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

#if defined(AF_UNIX)
	if (socket_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int stress_sctp_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int socket_opts,
	const int socket_type,
	const int socket_port,
	const int socket_domain)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	void *ptr = MAP_FAILED;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(socket_domain, socket_type, 0)) < 0) {
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
		socket_domain, socket_port,
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

	/*
	 * Some systems allow us to mmap onto the fd
	 * so try and do this just because we can
	 */
	ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE, fd, 0);

	do {
		int sfd;

		if (!keep_stressing())
			break;

#if defined(HAVE_ACCEPT4)
		/*  Randomly use accept or accept4 to exercise both */
		if (mwc1()) {
			sfd = accept4(fd, (struct sockaddr *)NULL, NULL, SOCK_CLOEXEC);
		} else {
			sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		}
#else
		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
#endif
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
#if defined(SOL_TCP) && defined(TCP_QUICKACK)
			{
				int ret, one = 1;
				/*
				 * We try do to a TCP_QUICKACK, failing is OK as
				 * it's just a faster optimization option
				 */
				ret = setsockopt(fd, SOL_TCP, TCP_QUICKACK, &one, sizeof(one));
				(void)ret;
			}
#endif

#if defined(SOL_TCP) && defined(HAVE_NETINET_TCP_H)
			if (g_opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
				int one = 1;

				if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
					pr_inf("%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					g_opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#endif
			(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));
			switch (socket_opts) {
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
				(void)memset(&msg, 0, sizeof(msg));
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
				(void)memset(msgvec, 0, sizeof(msgvec));
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
				pr_err("%s: bad option %d\n", args->name, socket_opts);
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
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
#if defined(AF_UNIX)
	if (addr && (socket_domain == AF_UNIX)) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);


	return rc;
}

/*
 *  stress_sock
 *	stress by heavy socket I/O
 */
static int stress_sock(const args_t *args)
{
	pid_t pid, ppid = getppid();
	int socket_opts = SOCKET_OPT_SEND;
	int socket_type = SOCK_STREAM;
	int socket_port = DEFAULT_SOCKET_PORT;
	int socket_domain = AF_INET;

	(void)get_setting("sock-opts", &socket_opts);
	(void)get_setting("sock-type", &socket_type);
	(void)get_setting("sock-port", &socket_port);
	(void)get_setting("sock-domain", &socket_domain);

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sctp_client(args, ppid, socket_type,
			socket_port, socket_domain);
		_exit(EXIT_SUCCESS);
	} else {
		return stress_sctp_server(args, pid, ppid, socket_opts,
			socket_type, socket_port, socket_domain);
	}
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sock_domain,	stress_set_socket_domain },
	{ OPT_sock_opts,	stress_set_socket_opts },
	{ OPT_sock_type,	stress_set_socket_type },
	{ OPT_sock_port,	stress_set_socket_port },
	{ 0,			NULL }
};

stressor_info_t stress_sock_info = {
	.stressor = stress_sock,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
