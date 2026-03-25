/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-net.h"
#include "core-signal.h"

#include <sys/ioctl.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_SYS_XATTR_H)
#include <sys/xattr.h>
#undef HAVE_ATTR_XATTR_H
#elif defined(HAVE_ATTR_XATTR_H)
#include <attr/xattr.h>
#endif
/*  Sanity check */
#if defined(HAVE_SYS_XATTR_H) &&        \
    defined(HAVE_ATTR_XATTR_H)
#error cannot have both HAVE_SYS_XATTR_H and HAVE_ATTR_XATTR_H
#endif

#define DEFAULT_SOCKABUSE_PORT	(12000)

#define MSGVEC_SIZE		(4)
#define SOCKET_BUF		(8192)	/* Socket I/O buffer size */

static const stress_help_t help[] = {
	{ NULL, "sockabuse N",		"start N workers abusing socket I/O" },
	{ NULL,	"sockabuse-ops N",	"stop after N socket abusing bogo operations" },
	{ NULL,	"sockabuse-port P",	"use socket ports P to P + number of workers - 1" },
	{ NULL,	NULL,			NULL }
};

static const int sockabuse_domains[] = {
#if defined(AF_UNIX)
	AF_UNIX,
#endif
#if defined(AF_LOCAL)
	AF_LOCAL,
#endif
#if defined(AF_INET)
	AF_INET,
#endif
#if defined(AF_AX25)
	AF_AX25,
#endif
#if defined(AF_IPX)
	AF_IPX,
#endif
#if defined(AF_APPLETALK)
	AF_APPLETALK,
#endif
#if defined(AF_NETROM)
	AF_NETROM,
#endif
#if defined(AF_BRIDGE)
	AF_BRIDGE,
#endif
#if defined(AF_ATMPVC)
	AF_ATMPVC,
#endif
#if defined(AF_X25)
	AF_X25,
#endif
#if defined(AF_INET6)
	AF_INET6,
#endif
#if defined(AF_ROSE)
	AF_ROSE,
#endif
#if defined(AF_DECnet)
	AF_DECnet,
#endif
#if defined(AF_NETBEUI)
	AF_NETBEUI,
#endif
#if defined(AF_SECURITY)
	AF_SECURITY,
#endif
#if defined(AF_KEY)
	AF_KEY,
#endif
#if defined(AF_NETLINK)
	AF_NETLINK,
#endif
#if defined(AF_ROUTE)
	AF_ROUTE,
#endif
#if defined(AF_PACKET)
	AF_PACKET,
#endif
#if defined(AF_ASH)
	AF_ASH,
#endif
#if defined(AF_ECONET)
	AF_ECONET,
#endif
#if defined(AF_ATMSVC)
	AF_ATMSVC,
#endif
#if defined(AF_RDS)
	AF_RDS,
#endif
#if defined(AF_SNA)
	AF_SNA,
#endif
#if defined(AF_IRDA)
	AF_IRDA,
#endif
#if defined(AF_PPPOX)
	AF_PPPOX,
#endif
#if defined(AF_WANPIPE)
	AF_WANPIPE,
#endif
#if defined(AF_LLC)
	AF_LLC,
#endif
#if defined(AF_IB)
	AF_IB,
#endif
#if defined(AF_MPLS)
	AF_MPLS,
#endif
#if defined(AF_CAN)
	AF_CAN,
#endif
#if defined(AF_TIPC)
	AF_TIPC,
#endif
#if defined(AF_BLUETOOTH)
	AF_BLUETOOTH,
#endif
#if defined(AF_IUCV)
	AF_IUCV,
#endif
#if defined(AF_RXRPC)
	AF_RXRPC,
#endif
#if defined(AF_ISDN)
	AF_ISDN,
#endif
#if defined(AF_PHONET)
	AF_PHONET,
#endif
#if defined(AF_IEEE802154)
	AF_IEEE802154,
#endif
#if defined(AF_CAIF)
	AF_CAIF,
#endif
#if defined(AF_NFC)
	AF_NFC,
#endif
#if defined(AF_VSOCK)
	AF_VSOCK,
#endif
#if defined(AF_KCM)
	AF_KCM,
#endif
#if defined(AF_QIPCRTR)
	AF_QIPCRTR,
#endif
#if defined(AF_SMC)
	AF_SMC,
#endif
#if defined(AF_XDP)
	AF_XDP,
#endif
#if defined(AF_MCTP)
	AF_MCTP,
#endif
};

static const int sockabuse_types[] = {
        SOCK_STREAM,
        SOCK_DGRAM,
        SOCK_RAW,
#if defined(__linux__) ||	\
    defined(__FreeBSD__) ||	\
    defined(__NetBSD__) ||	\
    defined(__OpenBSD__) ||	\
    defined(__DragonFly__) ||	\
    defined(__sun__)
        SOCK_RDM,
#endif
        SOCK_SEQPACKET,
#if defined(__linux__)
        SOCK_DCCP,
        SOCK_PACKET,
#endif
};

static const int sockabuse_sockopts[] = {
#if defined(SO_ACCEPTCONN)
	SO_ACCEPTCONN,
#endif
#if defined(SO_ATTACH_BPF)
	SO_ATTACH_BPF,
#endif
#if defined(SO_ATTACH_FILTER)
	SO_ATTACH_FILTER,
#endif
#if defined(SO_ATTACH_REUSEPORT_CBPF)
	SO_ATTACH_REUSEPORT_CBPF,
#endif
#if defined(SO_ATTACH_REUSEPORT_EBPF)
	SO_ATTACH_REUSEPORT_EBPF,
#endif
#if defined(SO_BINDTODEVICE)
	SO_BINDTODEVICE,
#endif
#if defined(SO_BINDTOIFINDEX)
	SO_BINDTOIFINDEX,
#endif
#if defined(SO_BPF_EXTENSIONS)
	SO_BPF_EXTENSIONS,
#endif
#if defined(SO_BROADCAST)
	SO_BROADCAST,
#endif
#if defined(SO_BSDCOMPAT)
	SO_BSDCOMPAT,
#endif
#if defined(SO_BUF_LOCK)
	SO_BUF_LOCK,
#endif
#if defined(SO_BUSY_POLL)
	SO_BUSY_POLL,
#endif
#if defined(SO_BUSY_POLL_BUDGET)
	SO_BUSY_POLL_BUDGET,
#endif
#if defined(SO_CNX_ADVICE)
	SO_CNX_ADVICE,
#endif
#if defined(SO_COOKIE)
	SO_COOKIE,
#endif
#if defined(SO_DEBUG)
	SO_DEBUG,
#endif
#if defined(SO_DETACH_FILTER)
	SO_DETACH_FILTER,
#endif
#if defined(SO_DETACH_REUSEPORT_BPF)
	SO_DETACH_REUSEPORT_BPF,
#endif
#if defined(SO_DEVMEM_DMABUF)
	SO_DEVMEM_DMABUF,
#endif
#if defined(SO_DEVMEM_DONTNEED)
	SO_DEVMEM_DONTNEED,
#endif
#if defined(SO_DEVMEM_LINEAR)
	SO_DEVMEM_LINEAR,
#endif
#if defined(SO_DOMAIN)
	SO_DOMAIN,
#endif
#if defined(SO_DONTROUTE)
	SO_DONTROUTE,
#endif
#if defined(SO_ERROR)
	SO_ERROR,
#endif
#if defined(SO_INCOMING_CPU)
	SO_INCOMING_CPU,
#endif
#if defined(SO_INCOMING_NAPI_ID)
	SO_INCOMING_NAPI_ID,
#endif
#if defined(SO_INQ)
	SO_INQ,
#endif
#if defined(SO_KEEPALIVE)
	SO_KEEPALIVE,
#endif
#if defined(SO_LINGER)
	SO_LINGER,
#endif
#if defined(SO_LOCK_FILTER)
	SO_LOCK_FILTER,
#endif
#if defined(SO_MARK)
	SO_MARK,
#endif
#if defined(SO_MAX_PACING_RATE)
	SO_MAX_PACING_RATE,
#endif
#if defined(SO_MEMINFO)
	SO_MEMINFO,
#endif
#if defined(SO_NETNS_COOKIE)
	SO_NETNS_COOKIE,
#endif
#if defined(SO_NO_CHECK)
	SO_NO_CHECK,
#endif
#if defined(SO_NOFCS)
	SO_NOFCS,
#endif
#if defined(SO_OOBINLINE)
	SO_OOBINLINE,
#endif
#if defined(SO_PASSCRED)
	SO_PASSCRED,
#endif
#if defined(SO_PASSPIDFD)
	SO_PASSPIDFD,
#endif
#if defined(SO_PASSRIGHTS)
	SO_PASSRIGHTS,
#endif
#if defined(SO_PASSSEC)
	SO_PASSSEC,
#endif
#if defined(SO_PEEK_OFF)
	SO_PEEK_OFF,
#endif
#if defined(SO_PEERCRED)
	SO_PEERCRED,
#endif
#if defined(SO_PEERGROUPS)
	SO_PEERGROUPS,
#endif
#if defined(SO_PEERNAME)
	SO_PEERNAME,
#endif
#if defined(SO_PEERPIDFD)
	SO_PEERPIDFD,
#endif
#if defined(SO_PEERSEC)
	SO_PEERSEC,
#endif
#if defined(SO_PREFER_BUSY_POLL)
	SO_PREFER_BUSY_POLL,
#endif
#if defined(SO_PRIORITY)
	SO_PRIORITY,
#endif
#if defined(SO_PROTOCOL)
	SO_PROTOCOL,
#endif
#if defined(SO_RCVBUF)
	SO_RCVBUF,
#endif
#if defined(SO_RCVBUFFORCE)
	SO_RCVBUFFORCE,
#endif
#if defined(SO_RCVLOWAT)
	SO_RCVLOWAT,
#endif
#if defined(SO_RCVMARK)
	SO_RCVMARK,
#endif
#if defined(SO_RCVPRIORITY)
	SO_RCVPRIORITY,
#endif
#if defined(SO_RCVTIMEO_NEW)
	SO_RCVTIMEO_NEW,
#endif
#if defined(SO_RCVTIMEO_OLD)
	SO_RCVTIMEO_OLD,
#endif
#if defined(SO_RESERVE_MEM)
	SO_RESERVE_MEM,
#endif
#if defined(SO_REUSEADDR)
	SO_REUSEADDR,
#endif
#if defined(SO_REUSEPORT)
	SO_REUSEPORT,
#endif
#if defined(SO_RXQ_OVFL)
	SO_RXQ_OVFL,
#endif
#if defined(SO_SECURITY_AUTHENTICATION)
	SO_SECURITY_AUTHENTICATION,
#endif
#if defined(SO_SECURITY_ENCRYPTION_NETWORK)
	SO_SECURITY_ENCRYPTION_NETWORK,
#endif
#if defined(SO_SECURITY_ENCRYPTION_TRANSPORT)
	SO_SECURITY_ENCRYPTION_TRANSPORT,
#endif
#if defined(SO_SELECT_ERR_QUEUE)
	SO_SELECT_ERR_QUEUE,
#endif
#if defined(SO_SNDBUF)
	SO_SNDBUF,
#endif
#if defined(SO_SNDBUFFORCE)
	SO_SNDBUFFORCE,
#endif
#if defined(SO_SNDLOWAT)
	SO_SNDLOWAT,
#endif
#if defined(SO_SNDTIMEO_NEW)
	SO_SNDTIMEO_NEW,
#endif
#if defined(SO_SNDTIMEO_OLD)
	SO_SNDTIMEO_OLD,
#endif
#if defined(SO_TIMESTAMPING_NEW)
	SO_TIMESTAMPING_NEW,
#endif
#if defined(SO_TIMESTAMPING_OLD)
	SO_TIMESTAMPING_OLD,
#endif
#if defined(SO_TIMESTAMP_NEW)
	SO_TIMESTAMP_NEW,
#endif
#if defined(SO_TIMESTAMPNS_NEW)
	SO_TIMESTAMPNS_NEW,
#endif
#if defined(SO_TIMESTAMPNS_OLD)
	SO_TIMESTAMPNS_OLD,
#endif
#if defined(SO_TIMESTAMP_OLD)
	SO_TIMESTAMP_OLD,
#endif
#if defined(SO_TXREHASH)
	SO_TXREHASH,
#endif
#if defined(SO_TXTIME)
	SO_TXTIME,
#endif
#if defined(SO_TYPE)
	SO_TYPE,
#endif
#if defined(SO_WIFI_STATUS)
	SO_WIFI_STATUS,
#endif
#if defined(SO_ZEROCOPY)
	SO_ZEROCOPY,
#endif
};

static uint8_t sockabuse_domain_type_flags[SIZEOF_ARRAY(sockabuse_domains)];

/*
 *  stress_sockabuse_socket()
 *	exercise various mix of socket domains, types and
 *	protocols
 */
static void stress_sockabuse_socket(stress_args_t *args)
{
	static size_t i = 0;
	size_t j;
	int fd;
	const int domain = sockabuse_domains[i];
	uint8_t flag = sockabuse_domain_type_flags[i] & 0x7f;

	if (UNLIKELY(SIZEOF_ARRAY(sockabuse_types) < 1))
		return;

	for (j = 0; j < SIZEOF_ARRAY(sockabuse_types); j++) {
		if (!stress_continue(args))
			return;
		if (flag & (1U << j)) {
			const int type = sockabuse_types[j];

			fd = socket(domain, type, 0);
			if (fd < 0) {
				flag &= ~(1U << j);
			} else {
				(void)shutdown(fd, SHUT_RDWR);
				(void)close(fd);
			}
			fd = socket(domain, type, (int)stress_mwc8());
			if (fd != -1) {
				(void)shutdown(fd, SHUT_RDWR);
				(void)close(fd);
			}
		}
	}
	sockabuse_domain_type_flags[i] = flag;

	i++;
	if (i >= SIZEOF_ARRAY(sockabuse_domains))
		i = 0;
}

static void stress_sockabuse_sockopts(const int fd)
{
#if defined(SOL_SOCKET)
	static size_t i = 0;
	char buf[1024];
	socklen_t len;

	if (UNLIKELY(SIZEOF_ARRAY(sockabuse_sockopts) < 1))
		return;

	len = 1024;
	(void)getsockopt(fd, SOL_SOCKET, sockabuse_sockopts[i], buf, &len);

	i++;
	if (i >= SIZEOF_ARRAY(sockabuse_sockopts))
		i = 0;
#else
	(void)fd;
#endif
}

/*
 *  stress_sockabuse_fd
 *	exercise and abuse the fd
 */
static void stress_sockabuse_fd(const int fd)
{
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	struct stat statbuf;
	void *ptr;
	int nfd;
	struct sockaddr addr;
	socklen_t addrlen;
#if defined(HAVE_FUTIMENS)
	struct timespec timespec[2];
#endif

	(void)shim_memset(&addr, 0, sizeof(addr));
	VOID_RET(int, connect(fd, &addr, sizeof(addr)));
	VOID_RET(int, shim_fdatasync(fd));
	VOID_RET(int, shim_fsync(fd));
	VOID_RET(int, shim_fallocate(fd, 0, 4096, 0));
	VOID_RET(int, fchdir(fd));
	VOID_RET(int, fchmod(fd, 0660));
	VOID_RET(int, fchown(fd, uid, gid));
#if defined(F_GETFD)
	VOID_RET(int, fcntl(fd, F_GETFD));
#else
	UNEXPECTED
#endif
#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_UN)
	VOID_RET(int, flock(fd, LOCK_UN));
#else
	UNEXPECTED
#endif
#if (defined(HAVE_SYS_XATTR_H) ||       \
     defined(HAVE_ATTR_XATTR_H)) &&     \
    defined(HAVE_SETXATTR) &&		\
    defined(XATTR_CREATE)
	VOID_RET(ssize_t, shim_fsetxattr(fd, "test", "value", 5, XATTR_CREATE));
#else
	UNEXPECTED
#endif
	VOID_RET(int, shim_fstat(fd, &statbuf));
	VOID_RET(int, ftruncate(fd, 0));
#if (defined(HAVE_SYS_XATTR_H) ||       \
     defined(HAVE_ATTR_XATTR_H)) &&     \
    defined(HAVE_FLISTXATTR)
	{
		char list[4096];

		VOID_RET(ssize_t, shim_flistxattr(fd, list, sizeof(list)));
	}
#else
	UNEXPECTED
#endif
#if defined(HAVE_FUTIMENS)
	{
		struct timeval now;

		if (LIKELY(gettimeofday(&now, NULL) == 0)) {
			timespec[0].tv_sec = now.tv_sec;
			timespec[1].tv_sec = now.tv_sec;

			timespec[0].tv_nsec = 1000 * now.tv_usec;
			timespec[1].tv_nsec = 1000 * now.tv_usec;
			VOID_RET(int, futimens(fd, timespec));
		}
	}
#else
	UNEXPECTED
#endif
	addrlen = sizeof(addr);
	VOID_RET(int, getpeername(fd, &addr, &addrlen));
#if defined(FIONREAD)
	{
		int n;

		VOID_RET(int, ioctl(fd, FIONREAD, &n));
	}
#else
	UNEXPECTED
#endif
#if defined(SEEK_SET)
	VOID_RET(off_t, lseek(fd, 0, SEEK_SET));
#else
	UNEXPECTED
#endif
	VOID_RET(int, shim_pidfd_send_signal(fd, SIGUSR1, NULL, 0));
	ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
	ptr = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
	nfd = dup(fd);
	VOID_RET(ssize_t, shim_copy_file_range(fd, 0, nfd, 0, 16, 0));
	if (LIKELY(nfd >= 0))
		(void)close(nfd);
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(SHIM_POSIX_FADV_RANDOM)
	VOID_RET(int, shim_posix_fadvise(fd, 0, 0, SHIM_POSIX_FADV_RANDOM));
#endif
	VOID_RET(int, shim_sync_file_range(fd, 0, 1, 0));
}

/*
 *  stress_sockabuse_client()
 *	client reader
 */
static int stress_sockabuse_client(
	stress_args_t *args,
	const pid_t mypid,
	const int sockabuse_port)
{
	struct sockaddr *addr;

	stress_parent_died_alarm();
	(void)stress_sched_settings_apply(true);

	do {
		char buf[SOCKET_BUF];
		int fd;
		ssize_t n;
		socklen_t addr_len = 0;
		uint64_t delay = 10000;

retry:
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_FAILURE;
		if (UNLIKELY((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if (UNLIKELY(stress_net_sockaddr_set(args->name, args->instance,
						     mypid, AF_INET, sockabuse_port,
						     &addr, &addr_len, NET_ADDR_ANY) < 0)) {
			return EXIT_FAILURE;
		}
		if (UNLIKELY(connect(fd, addr, addr_len) < 0)) {
			(void)shutdown(fd, SHUT_RDWR);
			(void)close(fd);
			(void)shim_usleep(delay);

			/* Backoff */
			delay += 10000;
			if (delay > 250000)
				delay = 250000;
			goto retry;
		}

		n = recv(fd, buf, sizeof(buf), 0);
		if (UNLIKELY(n < 0)) {
			if ((errno != EINTR) && (errno != ECONNRESET))
				pr_fail("%s: recv failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}

		stress_sockabuse_fd(fd);
		stress_sockabuse_sockopts(fd);

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_sockabuse_server()
 *	server writer
 */
static int stress_sockabuse_server(
	stress_args_t *args,
	const pid_t mypid,
	const int sockabuse_port)
{
	char buf[SOCKET_BUF];
	int fd;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	double t1 = 0.0, t2 = 0.0, dt;

	if (stress_signal_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	t1 = stress_time_now();
	do {
		int i;

		if (UNLIKELY((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0)) {
			rc = stress_exit_status(errno);
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			continue;
		}
#if defined(SOL_SOCKET)
		{
			int so_reuseaddr = 1;

			if (UNLIKELY(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
				     &so_reuseaddr, sizeof(so_reuseaddr)) < 0)) {
				rc = stress_exit_status(errno);
				pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(fd);
				continue;
			}
		}
#endif

		if (UNLIKELY(stress_net_sockaddr_set(args->name, args->instance,
						     mypid, AF_INET, sockabuse_port,
						     &addr, &addr_len, NET_ADDR_ANY) < 0)) {
			(void)close(fd);
			continue;
		}
		if (UNLIKELY(bind(fd, addr, addr_len) < 0)) {
			if (errno != EADDRINUSE) {
				rc = stress_exit_status(errno);
				pr_fail("%s: bind failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			(void)close(fd);
			continue;
		}
		if (UNLIKELY(listen(fd, 10) < 0)) {
			pr_fail("%s: listen failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;

			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}

		for (i = 0; i < 16; i++) {
			int sfd;

			if (UNLIKELY(!stress_continue(args)))
				break;

			sfd = accept(fd, (struct sockaddr *)NULL, NULL);
			if (LIKELY(sfd >= 0)) {
				struct sockaddr saddr;
				socklen_t len;
				int sndbuf;
				ssize_t n;

				len = sizeof(saddr);
				if (UNLIKELY(getsockname(fd, &saddr, &len) < 0)) {
					pr_fail("%s: getsockname failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(sfd);
					break;
				}
				len = sizeof(sndbuf);
#if defined(SOL_SOCKET) &&	\
    defined(SO_SNDBUF)
				if (UNLIKELY(getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0)) {
					pr_fail("%s: getsockopt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(sfd);
					break;
				}
#endif
				(void)shim_memset(buf, stress_ascii64[stress_bogo_get(args) & 63], sizeof(buf));

				n = send(sfd, buf, sizeof(buf), 0);
				if (UNLIKELY(n < 0)) {
					if ((errno != EINTR) && (errno != EPIPE))
						pr_fail("%s: send failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					stress_sockabuse_fd(sfd);
					(void)close(sfd);
					break;
				} else {
					msgs++;
				}
				stress_sockabuse_fd(sfd);
				(void)close(sfd);
			}
		}
		stress_bogo_inc(args);
		stress_sockabuse_sockopts(fd);
		stress_sockabuse_fd(fd);
		(void)close(fd);
		stress_sockabuse_socket(args);
	} while (stress_continue(args));
	t2 = stress_time_now();

die:
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);
	dt = t2 - t1;
	if (dt > 0.0)
		stress_metrics_set(args, "messages sent per sec",
			(double)msgs / dt, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

/*
 *  stress_sockabuse
 *	stress by heavy socket I/O
 */
static int stress_sockabuse(stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	int sockabuse_port = DEFAULT_SOCKABUSE_PORT;
	int rc = EXIT_SUCCESS, reserved_port, parent_cpu;

	(void)shim_memset(sockabuse_domain_type_flags, 0xff, sizeof(sockabuse_domain_type_flags));

	if (stress_signal_sigchld_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_setting_get("sockabuse-port", &sockabuse_port);

	sockabuse_port += args->instance;
	sockabuse_port = stress_net_port_wraparound(sockabuse_port);
	if (sockabuse_port > MAX_PORT)
		sockabuse_port -= (MAX_PORT - MIN_PORT + 1);
	reserved_port = stress_net_reserve_ports(args, sockabuse_port, sockabuse_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, sockabuse_port);
		return EXIT_NO_RESOURCE;
	}
	sockabuse_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, sockabuse_port);

	if (stress_signal_handler(args->name, SIGPIPE, stress_signal_stop_flag_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_cpu_get();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args))) {
			rc = EXIT_SUCCESS;
			goto finish;
		}
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_proc_state_set(args->name, STRESS_STATE_RUN);
		stress_make_it_fail_set();
		(void)stress_affinity_change_cpu(args, parent_cpu);

		rc = stress_sockabuse_client(args, mypid, sockabuse_port);
		_exit(rc);
	} else {
		rc = stress_sockabuse_server(args, mypid, sockabuse_port);
		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(sockabuse_port, sockabuse_port);

	return rc;
}


static const stress_opt_t opts[] = {
	{ OPT_sockabuse_port, "sockabuse-port", TYPE_ID_INT_PORT, MIN_PORT, MAX_PORT, NULL },
	END_OPT,
};

const stressor_info_t stress_sockabuse_info = {
	.stressor = stress_sockabuse,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
