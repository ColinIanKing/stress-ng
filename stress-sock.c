/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-affinity.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-madvise.h"
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

#define MIN_SOCKET_PORT		(1025)
#define MAX_SOCKET_PORT		(65535)
#define DEFAULT_SOCKET_PORT	(1000)

#define MIN_SOCKET_MSGS		(1)
#define MAX_SOCKET_MSGS		(10000000)
#define DEFAULT_SOCKET_MSGS	(5000)

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
} stress_sock_options_t;

static const stress_help_t help[] = {
	{ "S N", "sock N",		"start N workers exercising socket I/O" },
	{ NULL,	"sock-domain D",	"specify socket domain, default is ipv4" },
	{ NULL,	"sock-if I",		"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"sock-msgs N",		"number of messages to send per connection" },
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
 *  stress_set_sock_option()
 *	generic helper to set an option
 */
static int stress_set_sock_option(
	const char *setting,
	const stress_sock_options_t options[],
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
 *  stress_set_sock_opts()
 *	parse --sock-opts
 */
static int stress_set_sock_opts(const char *opt)
{
	static const stress_sock_options_t sock_opts[] = {
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

	return stress_set_sock_option("sock-opts", sock_opts, opt);
}

static const char * PURE stress_recv_func_str(const int sock_opts)
{
	switch (sock_opts) {
	case SOCKET_OPT_SEND:
		return "recv";
	case SOCKET_OPT_SENDMSG:
		return "recvmsg";
	case SOCKET_OPT_SENDMMSG:
		return "recvmmsg";
	}
	return "unknown";
}

/*
 *  stress_set_sock_type()
 *	parse --sock-type
 */
static int stress_set_sock_type(const char *opt)
{
	static const stress_sock_options_t sock_types[] = {
#if defined(SOCK_STREAM)
		{ "stream",	SOCK_STREAM  },
#endif
#if defined(SOCK_SEQPACKET)
		{ "seqpacket",	SOCK_SEQPACKET },
#endif
#if defined(SOCK_DGRAM)
		{ "dgram",	SOCK_DGRAM },
#endif
		{ NULL,		0 }
	};

	return stress_set_sock_option("sock-type", sock_types, opt);
}

/*
 *  stress_set_sock_msgs()
 *	set number of messages to send per connection
 */
static int stress_set_sock_msgs(const char *opt)
{
	size_t sock_msgs;

	sock_msgs = (size_t)stress_get_uint64(opt);
	stress_check_range("sock-msgs", (uint64_t)sock_msgs,
                MIN_SOCKET_MSGS, MAX_SOCKET_MSGS);
	return stress_set_setting("sock-msgs", TYPE_ID_SIZE_T, &sock_msgs);
}

/*
 *  stress_set_sock_port()
 *	set port to use
 */
static int stress_set_sock_port(const char *opt)
{
	int sock_port;

	stress_set_net_port("sock-port", opt,
		MIN_SOCKET_PORT, MAX_SOCKET_PORT, &sock_port);
	return stress_set_setting("sock-port", TYPE_ID_INT, &sock_port);
}

static int stress_set_sock_if(const char *name)
{
	return stress_set_setting("sock-if", TYPE_ID_STR, name);
}

/*
 *  stress_set_sock_protocol()
 *	parse --sock-protocol
 */
static int stress_set_sock_protocol(const char *opt)
{
	static const stress_sock_options_t sock_protocols[] = {
		{ "tcp",	IPPROTO_TCP},
#if defined(IPPROTO_MPTCP)
		{ "mptcp",	IPPROTO_MPTCP},
#endif
		{ NULL,		0 }
	};

	return stress_set_sock_option("sock-protocol", sock_protocols, opt);
}


/*
 *  stress_set_sock_domain()
 *	set the sock domain option
 */
static int stress_set_sock_domain(const char *name)
{
	int ret, sock_domain;

	ret = stress_set_net_domain(DOMAIN_ALL, "sock-domain",
				     name, &sock_domain);
	stress_set_setting("sock-domain", TYPE_ID_INT, &sock_domain);

	return ret;
}

/*
 *  stress_set_sock_zerocopy()
 *	set the sock zerocopy option
 */
static int stress_set_sock_zerocopy(const char *opt)
{
#if defined(MSG_ZEROCOPY)
	return stress_set_setting_true("sock-zerocopy", opt);
#else
	(void)opt;
	pr_inf("sock: cannot enable sock-zerocopy, MSG_ZEROCOPY is not available\n");
	return 0;
#endif
}

/*
 *  stress_get_congestion_controls()
 *	get congestion controls, currently only for AF_INET. ctrls is a
 *	pointer to an array of pointers to available congestion control names -
 *	the array is allocated by this function, or NULL if it fails. Returns
 *	the number of congestion controls, 0 if none found.
 */
static char **stress_get_congestion_controls(const int sock_domain, size_t *n_ctrls)
{
	static char ALIGN64 buf[4096];
	char *ptr, *ctrl;
	char **ctrls, **tmp;
	size_t n;
	ssize_t buf_len;

	*n_ctrls = 0;

	if (sock_domain != AF_INET)
		return NULL;

	buf_len = stress_system_read(PROC_CONG_CTRLS, buf, sizeof(buf));
	if (buf_len <= 0)
		return NULL;

	/*
	 *  Over-allocate ctrls, it is impossible to have more than
	 *  buf_len strings strok'd from the array buf.
	 */
	ctrls = calloc((size_t)buf_len, sizeof(char *));
	if (!ctrls)
		return NULL;

	for (n = 0, ptr = buf; (ctrl = strtok(ptr, " ")) != NULL; ptr = NULL) {
		char *newline = strchr(ctrl, '\n');

		if (newline)
			*newline = '\0';
		ctrls[n++] = ctrl;
	}

	if (n == 0) {
		free(ctrls);
		return NULL;
	}

	/* Shrink ctrls, hopefully should not fail, but check anyhow */
	tmp = realloc(ctrls, n * sizeof(char *));
	if (!tmp) {
		free(ctrls);
		return NULL;
	}
	*n_ctrls = n;
	return tmp;
}

/*
 *  stress_sock_ioctl()
 *	exercise various ioctl commands
 */
static void stress_sock_ioctl(
	const int fd,
	const int sock_domain,
	const bool rt)
{
	(void)fd;
	(void)sock_domain;
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

		(void)shim_memset(&ifc, 0, sizeof(ifc));
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
	if (sock_domain == AF_UNIX) {
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
		(void)shim_memset(&msg, 0, sizeof(msg));
		msg.msg_iov = vec;
		msg.msg_iovlen = 1;

		/* exercise invalid flags */
		VOID_RET(ssize_t, recvmsg(fd, &msg, ~0));

		/* exercise invalid fd */
		VOID_RET(ssize_t, recvmsg(~0, &msg, 0));
		break;
#if defined(HAVE_RECVMMSG)
	case SOCKET_OPT_RECVMMSG:
		(void)shim_memset(msgvec, 0, sizeof(msgvec));
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

/* SOL_SOCKET SO_* options that return int type */
static const int sol_socket_so_opts[] = {
#if defined(SO_DEBUG)
	SO_DEBUG,
#endif
#if defined(SO_DONTROUTE)
	SO_DONTROUTE,
#endif
#if defined(SO_BROADCAST)
	SO_BROADCAST,
#endif
#if defined(SO_KEEPALIVE)
	SO_KEEPALIVE,
#endif
#if defined(SO_TYPE)
	SO_TYPE,
#endif
#if defined(SO_DOMAIN)
	SO_DOMAIN,
#endif
#if defined(SO_OOBINLINE)
	SO_OOBINLINE,
#endif
#if defined(SO_NO_CHECK)
	SO_NO_CHECK,
#endif
#if defined(SO_BSDCOMPAT)
	SO_BSDCOMPAT,
#endif
#if defined(SO_TIMESTAMP_OLD)
	SO_TIMESTAMP_OLD,
#endif
#if defined(SO_TIMESTAMPNS_OLD)
	SO_TIMESTAMPNS_OLD,
#endif
#if defined(SO_TIMESTAMP_NEW)
	SO_TIMESTAMP_NEW,
#endif
#if defined(SO_TIMESTAMPNS_NEW)
	SO_TIMESTAMPNS_NEW,
#endif
#if defined(SO_RCVLOWAT)
	SO_RCVLOWAT,
#endif
#if defined(SO_SNDLOWAT)
	SO_SNDLOWAT,
#endif
#if defined(SO_PASSCRED)
	SO_PASSCRED,
#endif
#if defined(SO_PASSPIDFD)
	SO_PASSPIDFD,
#endif
#if defined(SO_ACCEPTCONN)
	SO_ACCEPTCONN,
#endif
#if defined(SO_MARK)
	SO_MARK,
#endif
#if defined(SO_RXQ_OVFL)
	SO_RXQ_OVFL,
#endif
#if defined(SO_WIFI_STATUS)
	SO_WIFI_STATUS,
#endif
#if defined(SO_PEEK_OFF)
	SO_PEEK_OFF,
#endif
#if defined(SO_NOFCS)
	SO_NOFCS,
#endif
#if defined(SO_LOCK_FILTER)
	SO_LOCK_FILTER,
#endif
#if defined(SO_BPF_EXTENSIONS)
	SO_BPF_EXTENSIONS,
#endif
#if defined(SO_SELECT_ERR_QUEUE)
	SO_SELECT_ERR_QUEUE,
#endif
#if defined(SO_BUSY_POLL)
	SO_BUSY_POLL,
#endif
#if defined(SO_PREFER_BUSY_POLL)
	SO_PREFER_BUSY_POLL,
#endif
#if defined(SO_INCOMING_CPU)
	SO_INCOMING_CPU,
#endif
#if defined(SO_INCOMING_NAPI_ID)
	SO_INCOMING_NAPI_ID,
#endif
#if defined(SO_BINDTOIFINDEX)
	SO_BINDTOIFINDEX,
#endif
#if defined(SO_BUF_LOCK)
	SO_BUF_LOCK,
#endif
#if defined(SO_RESERVE_MEM)
	SO_RESERVE_MEM,
#endif
#if defined(SO_TXREHASH)
	SO_TXREHASH,
#endif
};


/*
 *  stress_sock_client()
 *	client reader
 */
static int OPTIMIZE3 stress_sock_client(
	stress_args_t *args,
	char *buf,
	const pid_t mypid,
	const int sock_opts,
	const int sock_domain,
	const int sock_type,
	const int sock_protocol,
	const int sock_port,
	const char *sock_if,
	const bool rt,
	const bool sock_zerocopy)
{
	struct sockaddr *addr;
	size_t n_ctrls;
	char **ctrls;
	int recvflag = 0, rc = EXIT_FAILURE;
	uint64_t inq_bytes = 0, inq_samples = 0;
	uint32_t count = 0;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	ctrls = stress_get_congestion_controls(sock_domain, &n_ctrls);

	do {
		size_t i;
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
		double metric;

retry:
		if (!stress_continue_flag())
			goto free_controls;

		/* Exercise illegal socket family  */
		fd = socket(~0, sock_type, sock_protocol);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal socket type */
		fd = socket(sock_domain, ~0, sock_protocol);
		if (fd >= 0)
			(void)close(fd);

		/* Exercise illegal socket protocol */
		fd = socket(sock_domain, sock_type, ~0);
		if (fd >= 0)
			(void)close(fd);

		fd = socket(sock_domain, sock_type, sock_protocol);
		if (fd < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto free_controls;
		}
#if defined(MSG_ZEROCOPY) && defined(SO_ZEROCOPY)
		if (sock_zerocopy) {
			int so_zerocopy = 1;

			if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &so_zerocopy, sizeof(so_zerocopy)) == 0) {
				recvflag |= MSG_ZEROCOPY;
			} else {
				if (args->instance == 0)
					pr_inf("%s: cannot enable zerocopy on data being received\n", args->name);
			}
		}
#else
		(void)sock_zerocopy;
#endif

		if (stress_set_sockaddr_if(args->name, args->instance, mypid,
				sock_domain, sock_port, sock_if,
				&addr, &addr_len, NET_ADDR_ANY) < 0) {
			(void)close(fd);
			goto free_controls;
		}
		if (connect(fd, addr, addr_len) < 0) {
			const int errno_tmp = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno_tmp, strerror(errno_tmp));
				goto free_controls;
			}
			goto retry;
		}

#if defined(TCP_CONGESTION)
		/*
		 *  Randomly set congestion control
		 */
		if (n_ctrls > 0) {
			const int idx = stress_mwc16modn(n_ctrls);
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
		for (i = 0; i < SIZEOF_ARRAY(sol_socket_so_opts); i++) {
			int val = 0;
			socklen_t optlen = sizeof(val);

			VOID_RET(int, getsockopt(fd, SOL_SOCKET, sol_socket_so_opts[i], &val, &optlen));
		}
		if ((sock_domain == AF_INET) || (sock_domain == AF_INET6)) {

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
			size_t j;
			struct msghdr ALIGN64 msg;
			struct iovec ALIGN64 vec[MMAP_IO_SIZE / 16];
#if defined(HAVE_RECVMMSG)
			struct mmsghdr ALIGN64 msgvec[MSGVEC_SIZE];
			const int max_opt = 3;
#else
			const int max_opt = 2;
#endif
			const int opt = (sock_opts == SOCKET_OPT_RANDOM) ?
					stress_mwc8modn(max_opt): sock_opts;

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
				int val;

				VOID_RET(int, ioctl(fd, FIONREAD, &val));
#if defined(SIOCINQ)
				if (LIKELY(ioctl(fd, SIOCINQ, &val) == 0)) {
					inq_bytes += val;
					inq_samples++;
				}
#endif
#if defined(SIOCATMARK)
				/* and exercise SIOCATMARK */
				VOID_RET(int, ioctl(fd, SIOCATMARK, &val));
#endif
			}
#endif
			/*  Periodically exercise invalid recv calls */
			if ((count & 0x7ff) == 0)
				stress_sock_invalid_recv(fd, opt);

			/*
			 *  Receive using equivalent receive method
			 *  as the send
			 */
			switch (opt) {
			case SOCKET_OPT_RECV:
				n = recv(fd, buf, MMAP_IO_SIZE, recvflag);
				break;
			case SOCKET_OPT_RECVMSG:
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				(void)shim_memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				n = recvmsg(fd, &msg, recvflag);
				break;
#if defined(HAVE_RECVMMSG)
			case SOCKET_OPT_RECVMMSG:
				(void)shim_memset(msgvec, 0, sizeof(msgvec));
				for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				for (i = 0; i < MSGVEC_SIZE; i++) {
					msgvec[i].msg_hdr.msg_iov = vec;
					msgvec[i].msg_hdr.msg_iovlen = j;
				}
				n = recvmmsg(fd, msgvec, MSGVEC_SIZE, recvflag, NULL);
				if (LIKELY(n > 0)) {
					for (n = 0, i = 0; i < MSGVEC_SIZE; i++)
						n += msgvec[i].msg_len;
				}
				break;
#endif
			}
			if (UNLIKELY(n <= 0)) {
				if (n == 0)
					break;
				if ((errno != EINTR) && (errno != ECONNRESET))
					pr_fail("%s: %s failed, errno=%d (%s)\n",
						args->name, stress_recv_func_str(opt),
						errno, strerror(errno));
				break;
			}
			count++;
		} while (stress_continue(args));

		stress_sock_ioctl(fd, sock_domain, rt);
#if defined(AF_INET) && 	\
    defined(IPPROTO_IP)	&&	\
    defined(IP_MTU)
		/* Exercise IP_MTU */
		if (sock_domain == AF_INET) {
			int mtu;
			socklen_t mtu_len = sizeof(mtu);

			VOID_RET(int, getsockopt(fd, IPPROTO_IP, IP_MTU, &mtu, &mtu_len));
		}
#endif

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
		metric = (inq_samples > 0) ? (double)inq_bytes / (double)inq_samples : 0.0;
		stress_metrics_set(args, 2, "byte average in queue length",
			metric, STRESS_GEOMETRIC_MEAN);
	} while (stress_continue(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (sock_domain == AF_UNIX) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif

	rc = EXIT_SUCCESS;
free_controls:
	free(ctrls);

	return rc;
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
static int OPTIMIZE3 stress_sock_server(
	stress_args_t *args,
	char *buf,
	const pid_t pid,
	const pid_t ppid,
	const int sock_opts,
	const int sock_domain,
	const int sock_type,
	const int sock_protocol,
	const int sock_port,
	const char *sock_if,
	const bool rt,
	const bool sock_zerocopy)
{
	int fd;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	const size_t page_size = args->page_size;
	void *ptr = MAP_FAILED;
	const pid_t self = getpid();
	int sendflag = 0;
	double t, duration, metric;
	uint64_t outq_bytes = 0, outq_samples = 0;
	size_t sock_msgs = DEFAULT_SOCKET_MSGS;
#if defined(SIOCOUTQ)
	uint32_t count = 0;
#endif

	(void)stress_get_setting("sock-msgs", &sock_msgs);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	if ((fd = socket(sock_domain, sock_type, sock_protocol)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}

#if defined(MSG_ZEROCOPY) && defined(SO_ZEROCOPY)
	if (sock_zerocopy) {
		int so_zerocopy = 1;

		if (setsockopt(fd, SOL_SOCKET, SO_ZEROCOPY, &so_zerocopy, sizeof(so_zerocopy)) == 0) {
			sendflag |= MSG_ZEROCOPY;
		} else {
			if (args->instance == 0)
				pr_inf("%s: cannot enable zerocopy on data being sent\n", args->name);
		}
	}
#else
	(void)sock_zerocopy;
#endif
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

	if (stress_set_sockaddr_if(args->name, args->instance, ppid,
			sock_domain, sock_port, sock_if,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
		goto die_close;
	}

#if defined(SIOCGIFADDR) &&	\
    defined(HAVE_IFREQ)
	{
		struct ifreq ifaddr;

		(void)shim_memset(&ifaddr, 0, sizeof(ifaddr));
		(void)shim_strscpy(ifaddr.ifr_name, sock_if ? sock_if : "lo", sizeof(ifaddr.ifr_name));
		VOID_RET(int, ioctl(fd, SIOCGIFADDR, &ifaddr));
	}
#endif

	if (bind(fd, addr, addr_len) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: bind failed on port %d, errno=%d (%s)\n",
			args->name, sock_port, errno, strerror(errno));
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
	if (ptr != MAP_FAILED)
		(void)stress_madvise_mergeable(ptr, page_size);

	t = stress_time_now();
	do {
		int sfd;

		if (!stress_continue(args))
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
			size_t i, j, k;
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

#if defined(SIOCUNIXFILE)
			/* exercise SIOCUNIXFILE */
			if (sock_domain == AF_UNIX) {
				int unix_fd;

				unix_fd = ioctl(sfd, SIOCUNIXFILE, 0);
				if (unix_fd >= 0)
					(void)close(unix_fd);
			}
#endif

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
			(void)shim_memset(buf, stress_ascii64[stress_bogo_get(args) & 63], MMAP_IO_SIZE);

			opt = sock_opts;

			for (k = 0; (k < sock_msgs) && stress_continue(args); k++) {
				int flag = sendflag;

				if (UNLIKELY(sock_opts == SOCKET_OPT_RANDOM))
					opt = stress_mwc8modn(3);

				switch (opt) {
				case SOCKET_OPT_SEND:
					for (i = 16; i < MMAP_IO_SIZE; i += 16) {
retry_send:
						if (UNLIKELY(send(sfd, buf, i, flag) < 0)) {
							if (errno == ENOBUFS) {
								flag = 0;
								goto retry_send;
							}
							if (stress_send_error(errno)) {
								pr_fail("%s: send failed, errno=%d (%s)\n",
									args->name, errno, strerror(errno));
							}
							break;
						} else {
							msgs++;
						}
					}
					break;
				case SOCKET_OPT_SENDMSG:
					for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
						vec[j].iov_base = buf;
						vec[j].iov_len = i;
					}
					(void)shim_memset(&msg, 0, sizeof(msg));
					msg.msg_iov = vec;
					msg.msg_iovlen = j;
retry_sendmsg:
					if (UNLIKELY(sendmsg(sfd, &msg, flag) < 0)) {
						if (errno == ENOBUFS) {
							flag = 0;
							goto retry_sendmsg;
						}
						if (stress_send_error(errno)) {
							pr_fail("%s: sendmsg failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
						}
					} else {
						msgs += j;
					}
					break;
#if defined(HAVE_SENDMMSG)
				case SOCKET_OPT_SENDMMSG:
					(void)shim_memset(msgvec, 0, sizeof(msgvec));
					for (j = 0, i = 16; i < MMAP_IO_SIZE; i += 16, j++) {
						vec[j].iov_base = buf;
						vec[j].iov_len = i;
					}
					for (i = 0; i < MSGVEC_SIZE; i++) {
						msgvec[i].msg_hdr.msg_iov = vec;
						msgvec[i].msg_hdr.msg_iovlen = j;
					}
retry_sendmmsg:
					if (UNLIKELY(sendmmsg(sfd, msgvec, MSGVEC_SIZE, flag) < 0)) {
						if (errno == ENOBUFS) {
							flag = 0;
							goto retry_sendmmsg;
						}
						if (stress_send_error(errno)) {
							pr_fail("%s: sendmmsg failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
						}
					} else {
						msgs += (MSGVEC_SIZE * j);
					}
					break;
#endif
				default:
					/* Should never happen */
					pr_err("%s: bad option %d\n", args->name, sock_opts);
					(void)close(sfd);
					goto die_close;
				}
				stress_bogo_inc(args);
			}
			if (UNLIKELY(getpeername(sfd, &saddr, &len) < 0)) {
				if (errno != ENOTCONN)
					pr_fail("%s: getpeername failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
			}
#if defined(SIOCOUTQ)
			if ((count & 0x3ff) == 0) {
				int outq_len;

				if (LIKELY(ioctl(sfd, SIOCOUTQ, &outq_len) == 0)) {
					outq_bytes += outq_len;
					outq_samples++;
				}
			}
			count++;
#endif
			stress_sock_ioctl(fd, sock_domain, rt);
			stress_read_fdinfo(self, sfd);

			(void)close(sfd);
		}

#if defined(HAVE_ACCEPT4)
		/*
		 *  Exercise accept4 with invalid flags
		 */
		sfd = accept4(fd, (struct sockaddr *)NULL, NULL, ~0);
		if (sfd >= 0)
			(void)close(sfd);
#endif
	} while (stress_continue(args));

	duration = stress_time_now() - t;
	metric = (duration > 0.0) ? (double)msgs / duration : 0.0;
	stress_metrics_set(args, 0, "messages sent per sec",
		metric, STRESS_HARMONIC_MEAN);
	metric = (outq_samples > 0) ? (double)outq_bytes / (double)outq_samples : 0.0;
	stress_metrics_set(args, 1, "byte average out queue length",
		metric, STRESS_HARMONIC_MEAN);

die_close:
	(void)close(fd);
die:
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, page_size);
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && (sock_domain == AF_UNIX)) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	if (pid)
		(void)stress_kill_pid_wait(pid, NULL);
	return rc;
}

static void stress_sock_sigpipe_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
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
static int stress_sock(stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	int sock_opts = SOCKET_OPT_SEND;
	int sock_domain = AF_INET;
	int sock_type = SOCK_STREAM;
	int sock_port = DEFAULT_SOCKET_PORT;
	int sock_protocol = 0;
	int sock_zerocopy = false;
	int rc = EXIT_SUCCESS, reserved_port, parent_cpu;
	const bool rt = stress_sock_kernel_rt();
	char *mmap_buffer;
	char *sock_if = NULL;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("sock-if", &sock_if);
	(void)stress_get_setting("sock-domain", &sock_domain);
	(void)stress_get_setting("sock-type", &sock_type);
	(void)stress_get_setting("sock-protocol", &sock_protocol);
	(void)stress_get_setting("sock-port", &sock_port);
	(void)stress_get_setting("sock-opts", &sock_opts);
	(void)stress_get_setting("sock-zerocopy", &sock_zerocopy);

	if (sock_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(sock_if, sock_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, sock_if, stress_net_domain(sock_domain));
			sock_if = NULL;
		}
	}
	sock_port += args->instance;
	reserved_port = stress_net_reserve_ports(sock_port, sock_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, sock_port);
		return EXIT_NO_RESOURCE;
	}
	sock_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, sock_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	mmap_buffer = (char *)stress_mmap_populate(NULL, MMAP_BUF_SIZE,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (mmap_buffer == MAP_FAILED) {
		pr_inf("%s: cannot mmap I/O buffer, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args)) {
			rc = EXIT_SUCCESS;
			(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);
			goto finish;
		}
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)stress_change_cpu(args, parent_cpu);

		rc = stress_sock_client(args, mmap_buffer, mypid, sock_opts,
			sock_domain, sock_type, sock_protocol,
			sock_port, sock_if, rt, sock_zerocopy);
		(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);
		_exit(rc);
	} else {
		rc = stress_sock_server(args, mmap_buffer, pid, mypid, sock_opts,
			sock_domain, sock_type, sock_protocol,
			sock_port, sock_if, rt, sock_zerocopy);
		(void)munmap((void *)mmap_buffer, MMAP_BUF_SIZE);

	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(sock_port, sock_port);

	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sock_domain,	stress_set_sock_domain },
	{ OPT_sock_if,		stress_set_sock_if },
	{ OPT_sock_msgs,	stress_set_sock_msgs },
	{ OPT_sock_opts,	stress_set_sock_opts },
	{ OPT_sock_type,	stress_set_sock_type },
	{ OPT_sock_port,	stress_set_sock_port },
	{ OPT_sock_protocol,	stress_set_sock_protocol },
	{ OPT_sock_zerocopy,	stress_set_sock_zerocopy },
	{ 0,			NULL }
};

stressor_info_t stress_sock_info = {
	.stressor = stress_sock,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
