/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-net.h"

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UTSNAME_H)
#include <sys/utsname.h>
#else
UNEXPECTED
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#define MIN_SOCKET_PORT		(1024)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(5000)

#define MMAP_BUF_SIZE		(65536)
#define MMAP_IO_SIZE		(8192)	/* Must be less or equal to 8192 */

#define SOCKET_OPT_SEND		(0x00)
#define SOCKET_OPT_SENDMSG	(0x01)
#define SOCKET_OPT_SENDMMSG	(0x02)
#define SOCKET_OPT_RANDOM	(0x03)

#define SOCKET_OPT_RECV		(SOCKET_OPT_SEND)
#define SOCKET_OPT_RECVMSG	(SOCKET_OPT_SENDMSG)
#define SOCKET_OPT_RECVMMSG	(SOCKET_OPT_SENDMMSG)

#if !defined(IPPROTO_TCP)
#define IPPROTO_TCP		(0)
#endif

#define MSGVEC_SIZE		(4)

#define PROC_CONG_CTRLS		"/proc/sys/net/ipv4/tcp_allowed_congestion_control"

typedef struct {
	const char *optname;
	const int   optval;
} stress_socket_options_t;

static const stress_help_t help[] = {
	{ "S N", "sock N",		"start N workers exercising socket I/O" },
	{ NULL,	"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,	"sock-if I",		"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"sock-nodelay",		"disable Nagle algorithm, send data immediately" },
	{ NULL,	"sock-ops N",		"stop after N socket bogo operations" },
	{ NULL,	"sock-opts option", 	"socket options [send|sendmsg|sendmmsg]" },
	{ NULL,	"sock-port P",		"use socket ports P to P + number of workers - 1" },
	{ NULL, "sock-protocol",	"use socket protocol P, default is tcp, can be mptcp" },
	{ NULL,	"sock-type T",		"socket type (stream, seqpacket)" },
	{ NULL, "sock-zerocopy",	"enable zero copy sends" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_socket_option()
 *	generic helper to set an option
 */
static int stress_set_socket_option(
	const char *setting,
	const stress_socket_options_t options[],
	const char *opt)
{
	size_t i;

	for (i = 0; options[i].optname; i++) {
		if (!strcmp(opt, options[i].optname)) {
			int type = options[i].optval;

			stress_set_setting(setting, TYPE_ID_INT, &type);
			return 0;
		}
	}
	(void)fprintf(stderr, "%s option '%s' not known, options are:", setting, opt);
	for (i = 0; options[i].optname; i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", options[i].optname);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_socket_opts()
 *	parse --sock-opts
 */
static int stress_set_socket_opts(const char *opt)
{
	static const stress_socket_options_t socket_opts[] = {
		{ "random",	SOCKET_OPT_RANDOM },
		{ "send",	SOCKET_OPT_SEND },
		{ "sendmsg",	SOCKET_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
		{ "sendmmsg",	SOCKET_OPT_SENDMMSG },
#else
		UNEXPECTED
#endif
		{ NULL,		0 }
	};

	return stress_set_socket_option("sock-opts", socket_opts, opt);
}

/*
 *  stress_set_socket_type()
 *	parse --sock-type
 */
static int stress_set_socket_type(const char *opt)
{
	static const stress_socket_options_t socket_types[] = {
#if defined(SOCK_STREAM)
		{ "stream",	SOCK_STREAM  },
#endif
#if defined(SOCK_SEQPACKET)
		{ "seqpacket",	SOCK_SEQPACKET },
#endif
		{ NULL,		0 }
	};

	return stress_set_socket_option("sock-type", socket_types, opt);
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

static int stress_set_sock_if(const char *name)
{
	return stress_set_setting("sock-if", TYPE_ID_STR, name);
}

/*
 *  stress_set_socket_protocol()
 *	parse --sock-protocol
 */
static int stress_set_socket_protocol(const char *opt)
{
	static const stress_socket_options_t socket_protocols[] = {
		{ "tcp",	IPPROTO_TCP},
#if defined(IPPROTO_MPTCP)
		{ "mptcp",	IPPROTO_MPTCP},
#endif
		{ NULL,		0 }
	};

	return stress_set_socket_option("sock-protocol", socket_protocols, opt);
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
 *  stress_set_socket_zerocopy()
 *	set the socket zerocopy option
 */
static int stress_set_socket_zerocopy(const char *opt)
{
#if defined(MSG_ZEROCOPY)
	bool socket_zerocopy = true;

	(void)opt;
	return stress_set_setting("sock-zerocopy", TYPE_ID_BOOL, &socket_zerocopy);
#else
	(void)opt;
	pr_inf("sock: cannot enable sock-zerocopy, MSG_ZEROCOPY is not available\n");
	return 0;
#endif
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

	free(ctrls); /* cppcheck-suppress autovarInvalidDeallocation */
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
	char ALIGN64 buf[4096], *ptr, *ctrl;
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
			VOID_RET(int, ioctl(fd, FIOSETOWN, &own));
#endif
		(void)ret;
	}
#endif

#if defined(SIOCGPGRP)
	if (!rt) {
		int ret, own;

		ret = ioctl(fd, SIOCGPGRP, &own);
#if defined(SIOCSPGRP)
		if (ret == 0)
			VOID_RET(int, ioctl(fd, SIOCSPGRP, &own));
#endif
		(void)ret;
	}
#endif
#if defined(SIOCGIFCONF) && \
    defined(HAVE_IFCONF)
	if (!rt) {
		struct ifconf ifc;

		(void)memset(&ifc, 0, sizeof(ifc));
		VOID_RET(int, ioctl(fd, SIOCGIFCONF, &ifc));
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
		struct timeval tv;

		VOID_RET(int, ioctl(fd, SIOCGSTAMP, &tv));
	}
#endif

#if defined(SIOCGSTAMP_NEW) &&	\
    (ULONG_MAX > 4294967295UL)
	{
		struct timeval tv;

		VOID_RET(int, ioctl(fd, SIOCGSTAMP_NEW, &tv));
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
	char ALIGN64 buf[16];
	struct iovec ALIGN64 vec[1];
	struct msghdr msg;
#if defined(HAVE_RECVMMSG)
	struct mmsghdr ALIGN64 msgvec[MSGVEC_SIZE];
	struct timespec ts;
#endif

	switch (opt) {
	case SOCKET_OPT_RECV:
		/* exercise invalid flags */
		VOID_RET(ssize_t, recv(fd, buf, sizeof(buf), ~0));

		/* exercise invalid fd */
		VOID_RET(ssize_t, recv(~0, buf, sizeof(buf), 0));
		break;
	case SOCKET_OPT_RECVMSG:
		vec[0].iov_base = buf;
		vec[0].iov_len = sizeof(buf);
		(void)memset(&msg, 0, sizeof(msg));
		msg.msg_iov = vec;
		msg.msg_iovlen = 1;

		/* exercise invalid flags */
		VOID_RET(ssize_t, recvmsg(fd, &msg, ~0));

		/* exercise invalid fd */
		VOID_RET(ssize_t, recvmsg(~0, &msg, 0));
		break;
#if defined(HAVE_RECVMMSG)
	case SOCKET_OPT_RECVMMSG:
		(void)memset(msgvec, 0, sizeof(msgvec));
		vec[0].iov_base = buf;
		vec[0].iov_len = sizeof(buf);
		msgvec[0].msg_hdr.msg_iov = vec;
		msgvec[0].msg_hdr.msg_iovlen = 1;

		/* exercise invalid flags */
		VOID_RET(ssize_t, recvmmsg(fd, msgvec, MSGVEC_SIZE, ~0, NULL));

		/* exercise invalid fd */
		VOID_RET(ssize_t, recvmmsg(~0, msgvec, MSGVEC_SIZE, 0, NULL));

		/* exercise invalid timespec */
		ts.tv_sec = 0;
		ts.tv_nsec = 0;
		VOID_RET(ssize_t, recvmmsg(~0, msgvec, MSGVEC_SIZE, 0, &ts));
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
	char *buf,
	const pid_t ppid,
	const int socket_opts,
	const int socket_domain,
	const int socket_type,
	const int socket_protocol,
	const int socket_port,
	const char *socket_if,
	const bool rt,
	const bool socket_zerocopy)
{
	struct sockaddr *addr;
	size_t n_ctrls;
	char **ctrls;
	int recvflag = 0;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	n_ctrls = stress_get_congestion_controls(socket_domain, &ctrls);

#if defined(MSG_ZEROCOPY)
	if (socket_zerocopy)
		recvflag |= MSG_ZEROCOPY;
#else
	(void)socket_zerocopy;
#endif

	do {
		int fd;
		int retries = 0;
		static int count = 0;
		socklen_t addr_len = 0;
retry:
		if (!keep_stressing_flag()) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		/* Exercise illegal socket family  */
		fd = socket(~0, socket_type, socket_protocol);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal socket type */
		fd = socket(socket_domain, ~0, socket_protocol);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal socket protocol */
		fd = socket(socket_domain, socket_type, ~0);
		if (fd >= 0)
			(void)close(fd);

		fd = socket(socket_domain, socket_type, socket_protocol);
		if (fd < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr_if(args->name, args->instance, ppid,
			socket_domain, socket_port, socket_if,
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
				VOID_RET(int, setsockopt(fd, IPPROTO_IP, IP_MTU, &mtu, optlen));
			}
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
#if defined(SO_RESERVE_MEM)
		{
			const int mem = 4 * 1024 * 1024;
			socklen_t optlen = sizeof(mem);

			(void)setsockopt(fd, SOL_SOCKET, SO_RESERVE_MEM,
				&mem, optlen);
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
		if ((socket_domain == AF_INET) || (socket_domain == AF_INET6)) {
#if defined(TCP_NODELAY)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &val, optlen));
				}
			}
#endif
#if defined(TCP_CORK)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_CORK, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_CORK, &val, optlen));
				}
			}
#endif
#if defined(TCP_DEFER_ACCEPT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &val, optlen));
				}
			}
#endif
#if defined(TCP_KEEPCNT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &val, optlen));
				}
			}
#endif
#if defined(TCP_KEEPIDLE)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &val, optlen));
				}
			}
#endif
#if defined(TCP_KEEPINTVL)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &val, optlen));
				}
			}
#endif
#if defined(TCP_LINGER2)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_LINGER2, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_LINGER2, &val, optlen));
				}
			}
#endif
#if defined(TCP_MAXSEG)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_MAXSEG, &val, optlen));
				}
			}
#endif
#if defined(TCP_SYNCNT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_SYNCNT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_SYNCNT, &val, optlen));
				}
			}
#endif
#if defined(TCP_USER_TIMEOUT)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_USER_TIMEOUT, &val, optlen));
				}
			}
#endif
#if defined(TCP_WINDOW_CLAMP)
			{
				int val = 0, ret;
				socklen_t optlen = sizeof(val);

				ret = getsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, &optlen);
				if (ret == 0) {
					optlen = sizeof(val);
					VOID_RET(int, setsockopt(fd, IPPROTO_TCP, TCP_WINDOW_CLAMP, &val, optlen));
				}
			}
#endif
		}

		do {
			ssize_t n = 0;
			size_t i, j;
			char *recvfunc = "recv";
			struct msghdr ALIGN64 msg;
			struct iovec ALIGN64 vec[MMAP_IO_SIZE / 16];
#if defined(HAVE_RECVMMSG)
			struct mmsghdr ALIGN64 msgvec[MSGVEC_SIZE];
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
				size_t bytes = MMAP_IO_SIZE;

				VOID_RET(int, ioctl(fd, FIONREAD, &bytes));
				count = 0;

				if (bytes > MMAP_IO_SIZE)
					bytes = MMAP_IO_SIZE;

			}
#endif
			/*  Periodically exercise invalid recv calls */
			if ((count & 0x7ff) == 0)
				stress_sock_invalid_recv(fd, opt);
#if defined(SIOCINQ)
			{
				int pending;

				VOID_RET(int, ioctl(fd, SIOCINQ, &pending));
			}
#endif

			/*
			 *  Receive using equivalent receive method
			 *  as the send
			 */
			switch (opt) {
			case SOCKET_OPT_RECV:
				recvfunc = "recv";
				n = recv(fd, buf, MMAP_IO_SIZE, recvflag);
				break;
			case SOCKET_OPT_RECVMSG:
				recvfunc = "recvmsg";
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				(void)memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				n = recvmsg(fd, &msg, 0);
				break;
#if defined(HAVE_RECVMMSG)
			case SOCKET_OPT_RECVMMSG:
				recvfunc = "recvmmsg";
				(void)memset(msgvec, 0, sizeof(msgvec));
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				for (i = 0; i < MSGVEC_SIZE; i++) {
					msgvec[i].msg_hdr.msg_iov = vec;
					msgvec[i].msg_hdr.msg_iovlen = j;
				}
				n = recvmmsg(fd, msgvec, MSGVEC_SIZE, 0, NULL);
				if (n > 0) {
					for (n = 0, i = 0; i < MSGVEC_SIZE; i++)
						n += msgvec[i].msg_len;
				}
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
			int mtu;
			socklen_t mtu_len = sizeof(mtu);

			VOID_RET(int, getsockopt(fd, IPPROTO_IP, IP_MTU, &mtu, &mtu_len));
		}
#endif

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (socket_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	stress_free_congestion_controls(ctrls, n_ctrls);

	(void)kill(getppid(), SIGALRM);
}

static bool stress_send_error(const int err)
{
	return ((err != EINTR) &&
		(err != EPIPE) &&
		(err != ECONNRESET));
}

/*
 *  stress_sock_server()
 *	server writer
 */
static int stress_sock_server(
	const stress_args_t *args,
	char *buf,
	const pid_t pid,
	const pid_t ppid,
	const int socket_opts,
	const int socket_domain,
	const int socket_type,
	const int socket_protocol,
	const int socket_port,
	const char *socket_if,
	const bool rt,
	const bool socket_zerocopy)
{
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	void *ptr = MAP_FAILED;
	const pid_t self = getpid();
	int sendflag = 0;

#if defined(MSG_ZEROCOPY)
	if (socket_zerocopy)
		sendflag |= MSG_ZEROCOPY;
#else
	(void)socket_zerocopy;
#endif
	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	if ((fd = socket(socket_domain, socket_type, socket_protocol)) < 0) {
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
	/* exercise invalid setsockopt lengths */
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, 0);
	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, (socklen_t)-1);

	/* exercise invalid setsockopt fd */
	(void)setsockopt(-1, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));
	
	/* exercise invalid level */
	(void)setsockopt(fd, -1, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr));

	/* exercise invalid optname */
	(void)setsockopt(fd, SOL_SOCKET, -1, &so_reuseaddr, sizeof(so_reuseaddr));

	stress_set_sockaddr_if(args->name, args->instance, ppid,
		socket_domain, socket_port, socket_if,
		&addr, &addr_len, NET_ADDR_ANY);
	if (bind(fd, addr, addr_len) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: bind failed on port %d, errno=%d (%s)\n",
			args->name, socket_port, errno, strerror(errno));
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
			struct msghdr ALIGN64 msg;
			struct iovec ALIGN64 vec[MMAP_IO_SIZE / 16];
#if defined(HAVE_SENDMMSG)
			struct mmsghdr ALIGN64 msgvec[MSGVEC_SIZE];
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
				len = 0;
				VOID_RET(int, getsockname(fd, &saddr, &len));

				len = 1;
				VOID_RET(int, getsockname(fd, &saddr, &len));
			}

			len = sizeof(sndbuf);
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0) {
				pr_fail("%s: getsockopt failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			}
#if defined(SO_RESERVE_MEM)
		{
			const int mem = 4 * 1024 * 1024;
			socklen_t optlen = sizeof(mem);

			(void)setsockopt(fd, SOL_SOCKET, SO_RESERVE_MEM,
				&mem, optlen);
		}
#endif
#if defined(SOL_TCP) &&	\
    defined(TCP_QUICKACK)
			{
				int one = 1;

				/*
				 * We try do to a TCP_QUICKACK, failing is OK as
				 * it's just a faster optimization option
				 */
				VOID_RET(int, setsockopt(fd, SOL_TCP, TCP_QUICKACK, &one, sizeof(one)));
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
			(void)memset(buf, 'A' + (get_counter(args) % 26), MMAP_IO_SIZE);

			if (socket_opts == SOCKET_OPT_RANDOM)
				opt = stress_mwc8() % 3;
			else
				opt = socket_opts;

			switch (opt) {
			case SOCKET_OPT_SEND:
				for (i = 16; i < MMAP_IO_SIZE; i += 16) {
					ssize_t ret = send(sfd, buf, i, sendflag);
					if (ret < 0) {
						if (stress_send_error(errno))
							pr_fail("%s: send failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
						break;
					} else
						msgs++;
				}
				break;
			case SOCKET_OPT_SENDMSG:
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				(void)memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				if (sendmsg(sfd, &msg, 0) < 0) {
					if (stress_send_error(errno))
						pr_fail("%s: sendmsg failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
				} else
					msgs += j;
				break;
#if defined(HAVE_SENDMMSG)
			case SOCKET_OPT_SENDMMSG:
				(void)memset(msgvec, 0, sizeof(msgvec));
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				for (i = 0; i < MSGVEC_SIZE; i++) {
					msgvec[i].msg_hdr.msg_iov = vec;
					msgvec[i].msg_hdr.msg_iovlen = j;
				}
				if (sendmmsg(sfd, msgvec, MSGVEC_SIZE, 0) < 0) {
					if (stress_send_error(errno))
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

				VOID_RET(int, ioctl(sfd, SIOCOUTQ, &pending));
			}
#endif
			stress_sock_ioctl(fd, socket_domain, rt);
			stress_read_fdinfo(self, sfd);

			(void)close(sfd);
		}

#if defined(HAVE_ACCEPT4)
		/*
		 *  Exercise accept4 with invalid flags
		 */
		sfd = accept4(fd, (struct sockaddr *)NULL, NULL, ~0);
		if (sfd)
			(void)close(sfd);
#endif
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

		(void)shim_unlink(addr_un->sun_path);
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
	int socket_domain = AF_INET;
	int socket_type = SOCK_STREAM;
	int socket_port = DEFAULT_SOCKET_PORT;
#if defined(IPPROTO_TCP)
	int socket_protocol = IPPROTO_TCP;
#else
	int socket_protocol = 0;
#endif
	int socket_zerocopy = false;
	int rc = EXIT_SUCCESS;
	const bool rt = stress_sock_kernel_rt();
	char *mmap_buffer;
	char *socket_if = NULL;

	(void)stress_get_setting("sock-if", &socket_if);
	(void)stress_get_setting("sock-domain", &socket_domain);
	(void)stress_get_setting("sock-type", &socket_type);
	(void)stress_get_setting("sock-protocol", &socket_protocol);
	(void)stress_get_setting("sock-port", &socket_port);
	(void)stress_get_setting("sock-opts", &socket_opts);
	(void)stress_get_setting("sock-zerocopy", &socket_zerocopy);

	if (socket_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(socket_if, socket_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, socket_if, stress_net_domain(socket_domain));
			socket_if = NULL;
		}
	}
	socket_port += args->instance;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	mmap_buffer = (char *)mmap(NULL, MMAP_BUF_SIZE, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (mmap_buffer == MAP_FAILED) {
		pr_inf("%s: cannot mmap I/O buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		if (!keep_stressing(args)) {
			rc = EXIT_SUCCESS;
			goto finish;
		}
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sock_client(args, mmap_buffer, ppid, socket_opts,
			socket_domain, socket_type, socket_protocol,
			socket_port, socket_if, rt, socket_zerocopy);
		(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);
		_exit(rc);
	} else {
		rc = stress_sock_server(args, mmap_buffer, pid, ppid, socket_opts,
			socket_domain, socket_type, socket_protocol,
			socket_port, socket_if, rt, socket_zerocopy);
		(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);

	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sock_domain,	stress_set_socket_domain },
	{ OPT_sock_if,		stress_set_sock_if },
	{ OPT_sock_opts,	stress_set_socket_opts },
	{ OPT_sock_type,	stress_set_socket_type },
	{ OPT_sock_port,	stress_set_socket_port },
	{ OPT_sock_protocol,	stress_set_socket_protocol },
	{ OPT_sock_zerocopy,	stress_set_socket_zerocopy },
	{ 0,			NULL }
};

stressor_info_t stress_sock_info = {
	.stressor = stress_sock,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
