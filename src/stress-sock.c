/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define SOCKET_OPT_SEND		0x00
#define SOCKET_OPT_SENDMSG	0x01
#define SOCKET_OPT_SENDMMSG	0x02
#define SOCKET_OPT_RANDOM	0x03

#define MSGVEC_SIZE		(4)

#define PROC_CONG_CTRLS		"/proc/sys/net/ipv4/tcp_allowed_congestion_control"

typedef struct {
	const char *optname;
	int	   opt;
} stress_socket_opts_t;

typedef struct {
	const char *typename;
	const int   type;
} stress_socket_type_t;

static const stress_help_t help[] = {
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
	static const stress_socket_opts_t socket_opts[] = {
		{ "random",	SOCKET_OPT_RANDOM },
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

			stress_set_setting("sock-opts", TYPE_ID_INT, &opts);
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
	static const stress_socket_type_t socket_type[] = {
#if defined(SOCK_STREAM)
		{ "stream",	SOCK_STREAM  },
#endif
#if defined(SOCK_SEQPACKET)
		{ "seqpacket",	SOCK_SEQPACKET },
#endif
		{ NULL,		0 }
	};

	size_t i;

	for (i = 0; socket_type[i].typename; i++) {
		if (!strcmp(opt, socket_type[i].typename)) {
			int type = socket_type[i].type;

			stress_set_setting("sock-type", TYPE_ID_INT, &type);
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
	return stress_set_setting("sock-port", TYPE_ID_INT, &socket_port);
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
	stress_set_setting("sock-domain", TYPE_ID_INT, &socket_domain);

	return ret;
}

/*
 *  stress_free_congestion_controls()
 *	free congestion controls array
 */
static void stress_free_congestion_controls(char *ctrls[], const size_t n)
{
	size_t i;

	if (!ctrls)
		return;

	for (i = 0; i < n; i++)
		free(ctrls[i]);

	free(ctrls);
}

/*
 *  stress_get_congestion_controls()
 *	get congestion controls, currently only for AF_INET. ctrls is a
 *	pointer to an array of pointers to available congestion control names -
 *	the array is allocated by this function, or NULL if it fails. Returns
 *	the number of congestion controls, 0 if none found.
 */
static size_t stress_get_congestion_controls(const int socket_domain, char **ctrls[])
{
	char buf[4096], *ptr, *ctrl;
	char **array = NULL;
	size_t n_ctrls;

	*ctrls = NULL;

	if (socket_domain != AF_INET)
		return 0;

	if (system_read(PROC_CONG_CTRLS, buf, sizeof(buf)) < 0)
		return 0;

	for (n_ctrls = 0, ptr = buf; (ctrl = strtok(ptr, " ")) != NULL; ptr = NULL) {
		char **tmp, *newline = strchr(ctrl, '\n');

		if (newline)
			*newline = '\0';

		tmp = realloc(array , (sizeof(*array)) * (n_ctrls + 1));
		if (!tmp) {
			stress_free_congestion_controls(array, n_ctrls);
			return 0;
		}
		array = tmp;
		array[n_ctrls] = strdup(ctrl);
		if (!array[n_ctrls]) {
			stress_free_congestion_controls(array, n_ctrls);
			return 0;
		}
		n_ctrls++;
	}

	*ctrls = array;
	return n_ctrls;
}

/*
 *  stress_sock_ioctl()
 *	exercise various ioctl commands
 */
static void stress_sock_ioctl(
	const int fd,
	const int socket_domain,
	const bool rt)
{
	(void)fd;
	(void)socket_domain;
	(void)rt;

#if defined(FIOGETOWN)
	if (!rt) {
		int ret, own;

		ret = ioctl(fd, FIOGETOWN, &own);
#if defined(FIOSETOWN)
		if (ret == 0)
			ret = ioctl(fd, FIOSETOWN, &own);
		(void)ret;
#endif
	}
#endif

#if defined(SIOCGPGRP)
	if (!rt) {
		int ret, own;

		ret = ioctl(fd, SIOCGPGRP, &own);
#if defined(SIOCSPGRP)
		if (ret == 0)
			ret = ioctl(fd, SIOCSPGRP, &own);
#endif
		(void)ret;
	}
#endif
#if defined(SIOCGIFCONF) && \
    defined(HAVE_IFCONF)
	if (!rt) {
		int ret;
		struct ifconf ifc;

		ret = ioctl(fd, SIOCGIFCONF, &ifc);
		(void)ret;
	}
#endif

	/*
	 *  On some 32 bit arches on some kernels/libc flavours
	 *  struct __kernel_old_timeval is not defined and causes
	 *  this ioctl to break the build. So only build it for
	 *  64 bit arches as a workaround.
	 */
#if defined(SIOCGSTAMP) &&	\
    (ULONG_MAX > 4294967295UL)
	{
		int ret;
		struct timeval tv;

		ret = ioctl(fd, SIOCGSTAMP, &tv);
		(void)ret;
	}
#endif

#if defined(SIOCGSTAMP_NEW) &&	\
    (ULONG_MAX > 4294967295UL)
	{
		int ret;
		struct timeval tv;

		ret = ioctl(fd, SIOCGSTAMP_NEW, &tv);
		(void)ret;
	}
#endif

#if defined(SIOCGSKNS)
	{
		int ns_fd;

		ns_fd = ioctl(fd, SIOCGSKNS);
		if (ns_fd >= 0)
			(void)close(ns_fd);
	}
#endif

#if defined(__linux__) &&	\
    !defined(SIOCUNIXFILE)
#define SIOCUNIXFILE (SIOCPROTOPRIVATE + 0)
#endif

#if defined(SIOCUNIXFILE)
	if (socket_domain == AF_UNIX) {
		int fd_unixfile;

		fd_unixfile = ioctl(fd, SIOCUNIXFILE, 0);
		if (fd_unixfile >= 0)
			(void)close(fd_unixfile);
	}
#endif
}

/*
 *  stress_sock_invalid_recv()
 *	exercise invalid recv* calls
 */
static void stress_sock_invalid_recv(const int fd, const int opt)
{
	ssize_t n;
	char buf[16];
	struct iovec vec[1];
	struct msghdr msg;
#if defined(HAVE_RECVMMSG)
	struct mmsghdr msgvec[MSGVEC_SIZE];
	struct timespec ts;
#endif

	switch (opt) {
	case SOCKET_OPT_SEND:
		/* exercise invalid flags */
		n = recv(fd, buf, sizeof(buf), ~0);
		(void)n;

		/* exercise invalid fd */
		n = recv(~0, buf, sizeof(buf), 0);
		(void)n;
		break;
	case SOCKET_OPT_SENDMSG:
		vec[0].iov_base = buf;
		vec[0].iov_len = sizeof(buf);
		(void)memset(&msg, 0, sizeof(msg));
		msg.msg_iov = vec;
		msg.msg_iovlen = 1;

		/* exercise invalid flags */
		n = recvmsg(fd, &msg, ~0);
		(void)n;

		/* exercise invalid fd */
		n = recvmsg(~0, &msg, 0);
		(void)n;
		break;
#if defined(HAVE_RECVMMSG)
	case SOCKET_OPT_SENDMMSG:
		(void)memset(msgvec, 0, sizeof(msgvec));
		vec[0].iov_base = buf;
		vec[0].iov_len = sizeof(buf);
		msgvec[0].msg_hdr.msg_iov = vec;
		msgvec[0].msg_hdr.msg_iovlen = 1;

		/* exercise invalid flags */
		n = recvmmsg(fd, msgvec, MSGVEC_SIZE, ~0, NULL);
		(void)n;

		/* exercise invalid fd */
		n = recvmmsg(~0, msgvec, MSGVEC_SIZE, 0, NULL);
		(void)n;

		/* exercise invalid timespec */
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		n = recvmmsg(~0, msgvec, MSGVEC_SIZE, 0, &ts);
		(void)n;
		break;
#endif
	}
}

/*
 *  stress_sock_client()
 *	client reader
 */
static void stress_sock_client(
	const stress_args_t *args,
	const pid_t ppid,
	const int socket_opts,
	const int socket_type,
	const int socket_port,
	const int socket_domain,
	const bool rt)
{
	struct sockaddr *addr;
	size_t n_ctrls;
	char **ctrls;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	n_ctrls = stress_get_congestion_controls(socket_domain, &ctrls);

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		static int count = 0;
		socklen_t addr_len = 0;
retry:
		if (!keep_stressing_flag()) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(socket_domain, socket_type, 0)) < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			socket_domain, socket_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
			int errno_tmp = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno_tmp, strerror(errno_tmp));
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			goto retry;
		}

#if defined(TCP_CONGESTION)
		/*
		 *  Randomly set congestion control
		 */
		if (n_ctrls > 0) {
			const int idx = stress_mwc16() % n_ctrls;
			const char *control = ctrls[idx];
			char name[256];
			socklen_t len;

			len = (socklen_t)strlen(ctrls[idx]);
			(void)setsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, control, len);
			len = (socklen_t)sizeof(name);
			(void)getsockopt(fd, IPPROTO_TCP, TCP_CONGESTION, name, &len);
		}
#endif
#if defined(IP_MTU)
		{
			int ret, mtu;
			socklen_t optlen;

			optlen = sizeof(mtu);
			ret = getsockopt(fd, IPPROTO_IP, IP_MTU,
				&mtu, &optlen);
			if (ret == 0) {
				optlen = sizeof(mtu);
				ret = setsockopt(fd, IPPROTO_IP, IP_MTU,
					&mtu, optlen);
			}
			(void)ret;
		}
#endif
#if defined(IP_TOS) &&	\
    defined(IPTOS_THROUGHPUT)
		{
			char tos = IPTOS_THROUGHPUT;
			socklen_t optlen = sizeof(tos);

			(void)setsockopt(fd, IPPROTO_IP, IP_TOS,
				&tos, optlen);
			(void)getsockopt(fd, IPPROTO_IP, IP_TOS,
				&tos, &optlen);
		}
#endif
#if defined(SO_INCOMING_CPU)
		{
			int cpu;
			socklen_t optlen = sizeof(cpu);

			(void)getsockopt(fd, SOL_SOCKET, SO_INCOMING_CPU,
				&cpu, &optlen);
		}
#endif
		if (socket_domain == AF_INET || socket_domain == AF_INET6) {

#if defined(TCP_NODELAY)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_CORK)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_CORK, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_CORK, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_DEFER_ACCEPT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_KEEPCNT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_KEEPIDLE)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_KEEPINTVL)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_LINGER2)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_LINGER2, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_LINGER2, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_MAXSEG)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_SYNCNT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_SYNCNT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_SYNCNT, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_USER_TIMEOUT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &val, optlen);
				}
				(void)ret;
			}
#endif
#if defined(TCP_WINDOW_CLAMP)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					ret = setsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, optlen);
				}
				(void)ret;
			}
#endif
		}

		do {
			ssize_t n = 0;
			size_t i, j;
			char *recvfunc = "recv";
			struct msghdr msg;
			struct iovec vec[sizeof(buf)/16];
#if defined(HAVE_RECVMMSG)
			unsigned int msg_len = 0;
			struct mmsghdr msgvec[MSGVEC_SIZE];
			const int max_opt = 3;
#else
			const int max_opt = 2;
#endif
			const int opt = (socket_opts == SOCKET_OPT_RANDOM) ?
					stress_mwc8() % max_opt: socket_opts;

#if defined(FIONREAD)
			/*
			 *  Exercise FIONREAD ioctl. Linux supports
			 *  this also with SIOCINQ but lets try and
			 *  do the more standard way of peeking the
			 *  pending data size.  Do this infrequently
			 *  to ensure we exercise it without impacting
			 *  performance.
			 */
			if ((count & 0x3ff) == 0) {
				int ret;
				size_t bytes = sizeof(buf);

				ret = ioctl(fd, FIONREAD, &bytes);
				(void)ret;
				count = 0;

				if (bytes > sizeof(buf))
					bytes = sizeof(buf);

			}
#endif
			/*  Periodically exercise invalid recv calls */
			if ((count & 0x7ff) == 0)
				stress_sock_invalid_recv(fd, opt);
#if defined(SIOCINQ)
			{
				int pending;

				(void)ioctl(fd, SIOCINQ, &pending);
			}
#endif

			/*
			 *  Receive using equivalent receive method
			 *  as the send
			 */
			switch (opt) {
			case SOCKET_OPT_SEND:
				recvfunc = "recv";
				n = recv(fd, buf, sizeof(buf), 0);
				break;
			case SOCKET_OPT_SENDMSG:
				recvfunc = "recvmsg";
				for (j = 0, i = 16; i < sizeof(buf); i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				(void)memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				n = recvmsg(fd, &msg, 0);
				break;
#if defined(HAVE_RECVMMSG)
			case SOCKET_OPT_SENDMMSG:
				recvfunc = "recvmmsg";
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
				n = recvmmsg(fd, msgvec, MSGVEC_SIZE, 0, NULL);
				break;
#endif
			}
			if (n == 0)
				break;
			if (n < 0) {
				if ((errno != EINTR) && (errno != ECONNRESET))
					pr_fail("%s: %s failed, errno=%d (%s)\n",
						recvfunc, args->name,
						errno, strerror(errno));
				break;
			}
			count++;
		} while (keep_stressing(args));

		stress_sock_ioctl(fd, socket_domain, rt);
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
	} while (keep_stressing(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (socket_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	stress_free_congestion_controls(ctrls, n_ctrls);

	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_sock_server()
 *	server writer
 */
static int stress_sock_server(
	const stress_args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int socket_opts,
	const int socket_type,
	const int socket_port,
	const int socket_domain,
	const bool rt)
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
	const pid_t self = getpid();

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	if ((fd = socket(socket_domain, socket_type, 0)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	stress_set_sockaddr(args->name, args->instance, ppid,
		socket_domain, socket_port,
		&addr, &addr_len, NET_ADDR_ANY);
	if (bind(fd, addr, addr_len) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail("%s: listen failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
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

		if (!keep_stressing(args))
			break;

#if defined(HAVE_ACCEPT4)
		/*  Randomly use accept or accept4 to exercise both */
		if (stress_mwc1()) {
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
			int sndbuf, opt;
			struct msghdr msg;
			struct iovec vec[sizeof(buf)/16];
#if defined(HAVE_SENDMMSG)
			struct mmsghdr msgvec[MSGVEC_SIZE];
			unsigned int msg_len = 0;
#endif

			len = sizeof(saddr);
			if (getsockname(fd, &saddr, &len) < 0) {
				pr_fail("%s: getsockname failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			}
			/*
			 *  Exercise illegal sockname lengths
			 */
			{
				int ret;

				len = 0;
				ret = getsockname(fd, &saddr, &len);
				(void)ret;

				len = 1;
				ret = getsockname(fd, &saddr, &len);
				(void)ret;
			}

			len = sizeof(sndbuf);
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0) {
				pr_fail("%s: getsockopt failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			}
#if defined(SOL_TCP) &&	\
    defined(TCP_QUICKACK)
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

#if defined(SOL_TCP) &&	\
    defined(HAVE_NETINET_TCP_H)
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

			if (socket_opts == SOCKET_OPT_RANDOM)
				opt = stress_mwc8() % 3;
			else
				opt = socket_opts;

			switch (opt) {
			case SOCKET_OPT_SEND:
				for (i = 16; i < sizeof(buf); i += 16) {
					ssize_t ret = send(sfd, buf, i, 0);
					if (ret < 0) {
						if ((errno != EINTR) && (errno != EPIPE))
							pr_fail("%s: send failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
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
					if ((errno != EINTR) && (errno != EPIPE))
						pr_fail("%s: sendmsg failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
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
					if ((errno != EINTR) && (errno != EPIPE))
						pr_fail("%s: sendmmsg failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
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
				if (errno != ENOTCONN)
					pr_fail("%s: getpeername failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#if defined(SIOCOUTQ)
			{
				int pending;

				(void)ioctl(sfd, SIOCOUTQ, &pending);
			}
#endif
			stress_sock_ioctl(fd, socket_domain, rt);
			stress_read_fdinfo(self, sfd);

			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing(args));

die_close:
	(void)close(fd);
die:
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
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

static void stress_sock_sigpipe_handler(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
}

/*
 *  stress_sock_kernel_rt()
 * 	return true if kernel is PREEMPT_RT, true if
 * 	not sure, false if definitely not PREEMPT_RT.
 */
static bool stress_sock_kernel_rt(void)
{
#if defined(HAVE_UNAME) &&	\
    defined(HAVE_SYS_UTSNAME_H)
	struct utsname buf;

	if (uname(&buf) < 0)
		return true;	/* Not sure, assume rt */

	if (strstr(buf.version, "PREEMPT_RT"))
		return true;	/* Definitely rt */

	/* probably not RT */
	return false;
#else
	return true;		/* Not sure, assume rt */
#endif
}

/*
 *  stress_sock
 *	stress by heavy socket I/O
 */
static int stress_sock(const stress_args_t *args)
{
	pid_t pid, ppid = getppid();
	int socket_opts = SOCKET_OPT_SEND;
	int socket_type = SOCK_STREAM;
	int socket_port = DEFAULT_SOCKET_PORT;
	int socket_domain = AF_INET;
	const bool rt = stress_sock_kernel_rt();

	(void)stress_get_setting("sock-opts", &socket_opts);
	(void)stress_get_setting("sock-type", &socket_type);
	(void)stress_get_setting("sock-port", &socket_port);
	(void)stress_get_setting("sock-domain", &socket_domain);

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port + args->instance);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sock_client(args, ppid, socket_opts,
			socket_type, socket_port, socket_domain, rt);
		_exit(EXIT_SUCCESS);
	} else {
		int rc;

		rc = stress_sock_server(args, pid, ppid, socket_opts,
			socket_type, socket_port, socket_domain, rt);

		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

		return rc;
	}
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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
