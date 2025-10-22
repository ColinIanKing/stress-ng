/*
 * Copyright (C) 2025      Colin Ian King.
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
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-out-of-memory.h"

#if defined(HAVE_LINUX_IF_TUN_H)
#include <linux/if_tun.h>
#endif

#include <sys/ioctl.h>
#include <sched.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif
#if defined(HAVE_SYS_SELECT_H)
#include <sys/select.h>
#endif
#include <sys/socket.h>
#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#endif
#include <netinet/in.h>
#if defined(HAVE_IFADDRS_H)
#include <ifaddrs.h>
#endif
#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif
#if defined(HAVE_LINUX_IF_PACKET_H)
#include <linux/if_packet.h>
#endif
#if defined(HAVE_LINUX_CONNECTOR_H)
#include <linux/connector.h>
#endif
#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#endif
#if defined(HAVE_SYS_EVENTFD_H)
#include <sys/eventfd.h>
#endif
#if defined(HAVE_SYS_INOTIFY_H)
#include <sys/inotify.h>
#endif
#if defined(HAVE_SYS_TIMERFD_H)
#include <sys/timerfd.h>
#endif
#if defined(HAVE_SYS_STATVFS_H)
#include <sys/statvfs.h>
#endif
#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
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
#if defined(HAVE_SYS_SENDFILE_H)
#include <sys/sendfile.h>
#endif

#define FD_FLAG_READ	(0x0001)
#define FD_FLAG_WRITE	(0x0002)
#define FD_FLAG_RECV	(0x0004)
#define FD_FLAG_SEND	(0x0008)

typedef struct {
	int fd;		/* file descriptor */
	int flags;	/* rw flags */
} stress_fd_t;

typedef void (*open_func_t)(stress_fd_t *fd);
typedef void (*fd_func_t)(stress_fd_t *fd);

static char stress_fd_filename[PATH_MAX];
static double t_now;

static const stress_help_t help[] = {
	{ NULL,	"fd-abuse N",	"start N workers abusing file descriptors" },
	{ NULL,	"fd-abuse N",	"stop fd-abuse after bogo operations" },
	{ NULL,	NULL,		NULL }
};

static bool stress_fd_now(double *t, const double next)
{
	if (*t < t_now) {
		*t = t_now + next;
		return true;
	}
	return false;
}

static void stress_fd_bad_fd(stress_fd_t *fd)
{
	fd->fd = stress_get_bad_fd();
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}

static void stress_fd_open_null(stress_fd_t *fd)
{
	fd->fd = open("/dev/null", O_RDWR);
	fd->flags = FD_FLAG_WRITE;
}

static void stress_fd_open_zero(stress_fd_t *fd)
{
	fd->fd = open("/dev/zero", O_RDWR);
	fd->flags = FD_FLAG_READ;
}

static void stress_fd_creat_file(stress_fd_t *fd)
{
	if (*stress_fd_filename) {
		fd->fd = creat(stress_fd_filename, S_IRUSR | S_IWUSR);
		fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
	} else {
		fd->fd = -1;
		fd->flags = 0;
	}
}

static void stress_fd_open_file_ro(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDONLY);
	/* try to do writes on RDONLY file for EINVAL writes */
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}

static void stress_fd_open_file_wo(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
	/* try to do reads on WRONLY file for EINVAL reads */
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}

static void stress_fd_open_file_rw(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}

static void stress_fd_open_file_noaccess(stress_fd_t *fd)
{
	/* Linux allows this for ioctls, O_NOACCESS */
	fd->fd = open(stress_fd_filename, O_WRONLY | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}

#if defined(O_ASYNC)
static void stress_fd_open_file_rw_async(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_ASYNC, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(O_DIRECT)
static void stress_fd_open_file_rw_direct(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_DIRECT, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(O_NONBLOCK) &&	\
    defined(O_DIRECTORY) &&	\
    defined(HAVE_OPENAT)
static void stress_fd_open_temp_path(stress_fd_t *fd)
{
	const char *tmp = stress_get_temp_path();

	if (tmp) {
		fd->fd = openat(AT_FDCWD, tmp, O_RDWR | O_NONBLOCK | O_DIRECTORY);
		fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
	} else {
		fd->fd = -1;
		fd->flags = 0;
	}
}
#endif

#if defined(O_DSYNC)
static void stress_fd_open_file_rw_dsync(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_DSYNC, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(O_LARGEFILE)
static void stress_fd_open_file_rw_largefile(stress_fd_t *fd)
{
	if (O_LARGEFILE) {
		fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_LARGEFILE, S_IRUSR | S_IWUSR);
		fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
	} else {
		fd->fd = -1;
		fd->flags = 0;
	}

}
#endif

#if defined(O_NOATIME)
static void stress_fd_open_file_rw_noatime(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_NOATIME, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(O_NONBLOCK)
static void stress_fd_open_file_rw_nonblock(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_NONBLOCK, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(O_PATH)
static void stress_fd_open_file_path(stress_fd_t *fd)
{
	const char *tmp = stress_get_temp_path();

	if (tmp) {
		fd->fd = open(tmp, O_PATH);
		fd->flags = FD_FLAG_READ;
	} else {
		fd->fd = -1;
		fd->flags = 0;
	}
}
#endif

#if defined(O_SYNC)
static void stress_fd_open_file_rw_sync(stress_fd_t *fd)
{
	fd->fd = open(stress_fd_filename, O_RDWR | O_APPEND | O_SYNC, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

static void stress_fd_open_pipe_rd_end(stress_fd_t *fd)
{
	int fds[2];

	fd->flags = 0;
	if (pipe(fds) < 0) {
		fd->fd = -1;
		return;
	}
	(void)close(fds[1]);
	fd->fd = fds[0];
}

static void stress_fd_open_pipe_wr_end(stress_fd_t *fd)
{
	int fds[2];

	fd->flags = 0;
	if (pipe(fds) < 0) {
		fd->fd = -1;
		return;
	}
	(void)close(fds[0]);
	fd->fd = fds[1];
}

#if defined(HAVE_PIPE2)
static const int pipe2_flags[] = {
	0,
#if defined(O_CLOEXEC)
	O_CLOEXEC,
#endif
#if defined(O_DIRECT)
	O_DIRECT,
#endif
#if defined(O_NONBLOCK)
	O_NONBLOCK,
#endif
#if defined(O_NOTIFICATION_PIPE)
	O_NOTIFICATION_PIPE,
#endif
};
#endif

#if defined(HAVE_PIPE2)
static void stress_fd_open_pipe2_rd_end(stress_fd_t *fd)
{
	int fds[2];
	const int flag = pipe2_flags[stress_mwc8modn(SIZEOF_ARRAY(pipe2_flags))];

	fd->flags = 0;
	if (pipe2(fds, flag) < 0) {
		fd->fd = -1;
		return;
	}
	(void)close(fds[1]);
	fd->fd = fds[0];
}
#endif

#if defined(HAVE_PIPE2)
static void stress_fd_open_pipe2_wr_end(stress_fd_t *fd)
{
	int fds[2];
	const int flag = pipe2_flags[stress_mwc8modn(SIZEOF_ARRAY(pipe2_flags))];

	fd->flags = 0;
	if (pipe2(fds, flag) < 0) {
		fd->fd = -1;
	}
	(void)close(fds[0]);
	fd->fd = fds[1];
}
#endif

#if defined(HAVE_EVENTFD) &&	\
    defined(HAVE_SYS_EVENTFD_H)
static void stress_fd_open_eventfd(stress_fd_t *fd)
{
	fd->fd = eventfd(0, 0);
	fd->flags = 0;
}
#endif

#if defined(HAVE_MEMFD_CREATE)
static void stress_fd_open_memfd(stress_fd_t *fd)
{
	char name[64];

	(void)snprintf(name, sizeof(name), "memfd-%" PRIdMAX "-%" PRIu32,
		(intmax_t)getpid(), stress_mwc32());
	fd->fd = shim_memfd_create(name, 0);
	fd->flags = 0;
}
#endif

#if defined(__NR_memfd_secret)
static void stress_fd_open_memfd_secret(stress_fd_t *fd)
{
	fd->fd = shim_memfd_secret(0);
	fd->flags = 0;
}
#endif

#if defined(AF_INET) &&	\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_inet_stream(stress_fd_t *fd)
{
	fd->fd = socket(AF_INET, SOCK_STREAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_INET6) &&	\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_inet6_stream(stress_fd_t *fd)
{
	fd->fd = socket(AF_INET6, SOCK_STREAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_INET) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_inet_dgram(stress_fd_t *fd)
{
	fd->fd = socket(AF_INET, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_INET6) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_inet6_dgram(stress_fd_t *fd)
{
	fd->fd = socket(AF_INET6, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_UNIX) &&	\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_af_unix_stream(stress_fd_t *fd)
{
	fd->fd = socket(AF_UNIX, SOCK_STREAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_UNIX) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_unix_dgram(stress_fd_t *fd)
{
	fd->fd = socket(AF_UNIX, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_ALG) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_alg_seqpacket(stress_fd_t *fd)
{
	fd->fd = socket(AF_ALG, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_INET) &&		\
    defined(SOCK_DGRAM) &&	\
    defined(IPPROTO_ICMP)
static void stress_fd_open_sock_af_inet_dgram_icmp(stress_fd_t *fd)
{
	fd->fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_AX25) &&		\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_ax25(stress_fd_t *fd)
{
	fd->fd = socket(AF_AX25, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_X25) &&		\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_x25(stress_fd_t *fd)
{
	fd->fd = socket(AF_X25, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_ROSE) &&		\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_rose(stress_fd_t *fd)
{
	fd->fd = socket(AF_ROSE, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_IRDA) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_irda(stress_fd_t *fd)
{
	fd->fd = socket(AF_IRDA, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_WANPIPE) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_wanpipe(stress_fd_t *fd)
{
	fd->fd = socket(AF_WANPIPE, SOCK_RAW, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_IPX) &&		\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_ipx(stress_fd_t *fd)
{
	fd->fd = socket(AF_IPX, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_APPLETALK) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_appletalk(stress_fd_t *fd)
{
	fd->fd = socket(AF_APPLETALK, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif
#if defined(AF_PACKET) &&	\
    defined(SOCK_RAW) &&	\
    defined(ETH_P_ALL)
static void stress_fd_open_sock_af_packet(stress_fd_t *fd)
{
	fd->fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_KEY) &&	\
    defined(SOCK_RAW)
static void stress_fd_open_sock_af_key(stress_fd_t *fd)
{
	fd->fd = socket(AF_KEY, SOCK_RAW, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_NETLINK) &&	\
    defined(SOCK_DGRAM) &&	\
    defined(NETLINK_CONNECTOR)
static void stress_fd_open_sock_af_netlink(stress_fd_t *fd)
{
	fd->fd = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_NETROM) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_netrom(stress_fd_t *fd)
{
	fd->fd = socket(AF_NETROM, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_ATMPVC) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_atmpvc(stress_fd_t *fd)
{
	fd->fd = socket(AF_ATMPVC, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_RDS) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_rds(stress_fd_t *fd)
{
	fd->fd = socket(AF_RDS, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_IUCV) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_iucv(stress_fd_t *fd)
{
	fd->fd = socket(AF_IUCV, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_PHONET) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_phonet(stress_fd_t *fd)
{
	fd->fd = socket(AF_PHONET, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_IEEE802154) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_ieee802154(stress_fd_t *fd)
{
	fd->fd = socket(AF_IEEE802154, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_NFC) &&		\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_af_nfc(stress_fd_t *fd)
{
	fd->fd = socket(AF_NFC, SOCK_STREAM, 1);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_SMC) &&		\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_af_smc(stress_fd_t *fd)
{
	fd->fd = socket(AF_SMC, SOCK_STREAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_PPPOX) &&	\
    defined(SOCK_DGRAM)
static void stress_fd_open_sock_af_ppox(stress_fd_t *fd)
{
	fd->fd = socket(AF_PPPOX, SOCK_DGRAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_LLC) &&	\
    defined(SOCK_STREAM)
static void stress_fd_open_sock_af_llc(stress_fd_t *fd)
{
	fd->fd = socket(AF_LLC, SOCK_STREAM, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_CAN) &&	\
    defined(SOCK_RAW)
static void stress_fd_open_sock_af_can(stress_fd_t *fd)
{
	fd->fd = socket(AF_CAN, SOCK_RAW, 1);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_TIPC) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_tipc(stress_fd_t *fd)
{
	fd->fd = socket(AF_TIPC, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_BLUETOOTH) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_bluetooth(stress_fd_t *fd)
{
	fd->fd = socket(AF_BLUETOOTH, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_KCM) &&	\
    defined(SOCK_SEQPACKET)
static void stress_fd_open_sock_af_kcm(stress_fd_t *fd)
{
	fd->fd = socket(AF_KCM, SOCK_SEQPACKET, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

#if defined(AF_XDP) &&	\
    defined(SOCK_RAW)
static void stress_fd_open_sock_af_xdp(stress_fd_t *fd)
{
	fd->fd = socket(AF_XDP, SOCK_RAW, 0);
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;
}
#endif

static void stress_fd_open_socketpair(stress_fd_t *fd)
{
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) {
		fd->fd = -1;
		fd->flags = 0;
		return;
	}

	(void)close(sv[1]);
	fd->fd = sv[0];
	fd->flags = FD_FLAG_RECV | FD_FLAG_SEND;

}

#if defined(O_TMPFILE)
static void stress_fd_open_tmpfile(stress_fd_t *fd)
{
	fd->fd = open("/tmp", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
	fd->flags = FD_FLAG_READ | FD_FLAG_WRITE;
}
#endif

#if defined(HAVE_USERFAULTFD)
static void stress_fd_open_userfaultfd(stress_fd_t *fd)
{
	fd->fd = shim_userfaultfd(0);
	fd->flags = 0;
}
#endif

#if defined(HAVE_SYS_INOTIFY_H)
static void stress_fd_open_inotify_init(stress_fd_t *fd)
{
	fd->fd = inotify_init();
	fd->flags = 0;
}
#endif

#if defined(HAVE_PTSNAME)
static void stress_fd_open_ptxm(stress_fd_t *fd)
{
	fd->fd = open("/dev/ptmx", O_RDWR);
	fd->flags = 0;
}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
static void stress_fd_open_timerfd(stress_fd_t *fd)
{
	fd->fd = timerfd_create(CLOCK_REALTIME, 0);
	fd->flags = 0;
}
#endif

#if defined(HAVE_PIDFD_OPEN)
static void stress_fd_open_pidfd(stress_fd_t *fd)
{
	fd->fd = shim_pidfd_open(getpid(), 0);
	fd->flags = 0;
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE)
static void stress_fd_open_epoll_create(stress_fd_t *fd)
{
	fd->fd = epoll_create(1);
	fd->flags = 0;
}
#endif

static open_func_t open_funcs[] = {
	stress_fd_bad_fd,
	stress_fd_open_null,
	stress_fd_open_zero,
	stress_fd_creat_file,
	stress_fd_open_file_ro,
	stress_fd_open_file_wo,
	stress_fd_open_file_rw,
	stress_fd_open_file_noaccess,
#if defined(O_ASYNC)
	stress_fd_open_file_rw_async,
#endif
#if defined(O_DIRECT)
	stress_fd_open_file_rw_direct,
#endif
#if defined(O_DSYNC)
	stress_fd_open_file_rw_dsync,
#endif
#if defined(O_LARGEFILE)
	stress_fd_open_file_rw_largefile,
#endif
#if defined(O_NOATIME)
	stress_fd_open_file_rw_noatime,
#endif
#if defined(O_NONBLOCK)
	stress_fd_open_file_rw_nonblock,
#endif
#if defined(O_PATH)
	stress_fd_open_file_path,
#endif
#if defined(O_SYNC)
	stress_fd_open_file_rw_sync,
#endif
#if defined(O_NONBLOCK) &&	\
    defined(O_DIRECTORY) &&	\
    defined(HAVE_OPENAT)
	stress_fd_open_temp_path,
#endif
	stress_fd_open_pipe_rd_end,
	stress_fd_open_pipe_wr_end,
#if defined(HAVE_PIPE2)
	stress_fd_open_pipe2_rd_end,
#endif
#if defined(HAVE_PIPE2)
	stress_fd_open_pipe2_wr_end,
#endif
#if defined(HAVE_EVENTFD) &&	\
    defined(HAVE_SYS_EVENTFD_H)
	stress_fd_open_eventfd,
#endif
#if defined(HAVE_MEMFD_CREATE)
	stress_fd_open_memfd,
#endif
#if defined(__NR_memfd_secret)
	stress_fd_open_memfd_secret,
#endif
#if defined(AF_INET) &&	\
    defined(SOCK_STREAM)
	stress_fd_open_sock_inet_stream,
#endif
#if defined(AF_INET6) &&	\
    defined(SOCK_STREAM)
	stress_fd_open_sock_inet6_stream,
#endif
#if defined(AF_INET) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_inet_dgram,
#endif
#if defined(AF_INET6) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_inet6_dgram,
#endif
#if defined(AF_UNIX) &&	\
    defined(SOCK_STREAM)
	stress_fd_open_sock_af_unix_stream,
#endif
#if defined(AF_UNIX) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_unix_dgram,
#endif
#if defined(AF_ALG) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_alg_seqpacket,
#endif
#if defined(AF_INET) &&		\
    defined(SOCK_DGRAM) &&	\
    defined(IPPROTO_ICMP)
	stress_fd_open_sock_af_inet_dgram_icmp,
#endif
#if defined(AF_AX25) &&		\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_ax25,
#endif
#if defined(AF_X25) &&		\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_x25,
#endif
#if defined(AF_ROSE) &&		\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_rose,
#endif
#if defined(AF_IRDA) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_irda,
#endif
#if defined(AF_WANPIPE) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_wanpipe,
#endif
#if defined(AF_IPX) &&		\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_ipx,
#endif
#if defined(AF_APPLETALK) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_appletalk,
#endif
#if defined(AF_KEY) &&	\
    defined(SOCK_RAW)
	stress_fd_open_sock_af_key,
#endif
#if defined(AF_PACKET) &&	\
    defined(SOCK_RAW) &&	\
    defined(ETH_P_ALL)
	stress_fd_open_sock_af_packet,
#endif
#if defined(AF_NETLINK) &&	\
    defined(SOCK_DGRAM) &&	\
    defined(NETLINK_CONNECTOR)
	stress_fd_open_sock_af_netlink,
#endif
#if defined(AF_NETROM) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_netrom,
#endif
#if defined(AF_ATMPVC) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_atmpvc,
#endif
#if defined(AF_RDS) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_rds,
#endif
#if defined(AF_IUCV) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_iucv,
#endif
#if defined(AF_PHONET) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_phonet,
#endif
#if defined(AF_IEEE802154) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_ieee802154,
#endif
#if defined(AF_NFC) &&		\
    defined(SOCK_STREAM)
	stress_fd_open_sock_af_nfc,
#endif
#if defined(AF_SMC) &&		\
    defined(SOCK_STREAM)
	stress_fd_open_sock_af_smc,
#endif
#if defined(AF_PPPOX) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_ppox,
#endif
#if defined(AF_LLC) &&	\
    defined(SOCK_STREAM)
	stress_fd_open_sock_af_llc,
#endif
#if defined(AF_CAN) &&	\
    defined(SOCK_RAW)
	stress_fd_open_sock_af_can,
#endif
#if defined(AF_TIPC) &&	\
    defined(SOCK_DGRAM)
	stress_fd_open_sock_af_tipc,
#endif
#if defined(AF_BLUETOOTH) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_bluetooth,
#endif
#if defined(AF_KCM) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_kcm,
#endif
#if defined(AF_XDP) &&	\
    defined(SOCK_SEQPACKET)
	stress_fd_open_sock_af_xdp,
#endif
	stress_fd_open_socketpair,
#if defined(HAVE_USERFAULTFD)
	stress_fd_open_userfaultfd,
#endif
#if defined(O_TMPFILE)
	stress_fd_open_tmpfile,
#endif
#if defined(HAVE_SYS_INOTIFY_H)
	stress_fd_open_inotify_init,
#endif
#if defined(HAVE_PTSNAME)
	stress_fd_open_ptxm,
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
	stress_fd_open_timerfd,
#endif
#if defined(HAVE_PIDFD_OPEN)
	stress_fd_open_pidfd,
#endif
#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE)
	stress_fd_open_epoll_create,
#endif
};

#if defined(SOL_SOCKET)
static void stress_fd_sockopt_reuseaddr(stress_fd_t *fd)
{
	int so_reuseaddr = 1;

	(void)setsockopt(fd->fd, SOL_SOCKET, SO_REUSEADDR,
                &so_reuseaddr, sizeof(so_reuseaddr));
}
#endif

static void stress_fd_lseek(stress_fd_t *fd)
{
	static const int whence[] = {
		SEEK_SET,
		SEEK_END,
		SEEK_CUR,
#if defined(SEEK_DATA)
		SEEK_DATA,
#endif
#if defined(SEEK_HOLE)
		SEEK_HOLE,
#endif
	};
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(whence); i++) {
		VOID_RET(off_t, lseek(fd->fd, 0, whence[i]));
		VOID_RET(off_t, lseek(fd->fd, 1024, whence[i]));
		VOID_RET(off_t, lseek(fd->fd, (off_t)stress_mwc32(), whence[i]));
		VOID_RET(off_t, lseek(fd->fd, 1, whence[i]));
	}
}

static void stress_fd_dup(stress_fd_t *fd)
{
	int fd2;

	fd2 = dup(fd->fd);
	if (fd2 >= 0)
		(void)close(fd2);
}

static void stress_fd_dup2(stress_fd_t *fd)
{
	int fd2;

	fd2 = dup2(fd->fd, stress_mwc16() + 100);
	if (fd2 >= 0)
		(void)close(fd2);
}

#if defined(O_CLOEXEC)
static void stress_fd_dup3(stress_fd_t *fd)
{
	int fd2;

	fd2 = shim_dup3(fd->fd, stress_mwc16() + 100, O_CLOEXEC);
	if (fd2 >= 0)
		(void)close(fd2);
}
#endif

static void stress_fd_bind_af_inet(stress_fd_t *fd)
{
	struct sockaddr_in addr;

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_family = (sa_family_t)AF_INET;
	addr.sin_port = htons(40000);
	if (bind(fd->fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr)) == 0)
		(void)shutdown(fd->fd, SHUT_RDWR);
}

#if defined(AF_INET6)
static void stress_fd_bind_af_inet6(stress_fd_t *fd)
{
	struct sockaddr_in6 addr;
#if defined(__minix__)
	struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
#endif

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin6_addr = in6addr_loopback;
	addr.sin6_family = (sa_family_t)AF_INET6;
	addr.sin6_port = htons(40000);
	if (bind(fd->fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr)) == 0)
		(void)shutdown(fd->fd, SHUT_RDWR);
}
#endif

static void stress_fd_select_rd(stress_fd_t *fd)
{
	if ((fd->fd >= 0) && (fd->fd < FD_SETSIZE)) {
		fd_set rfds;
		struct timeval timeout;

		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_SET(fd->fd, &rfds);

		(void)select(fd->fd + 1, &rfds, NULL, NULL, &timeout);
	}
}

static void stress_fd_select_wr(stress_fd_t *fd)
{
	if ((fd->fd >= 0) && (fd->fd < FD_SETSIZE)) {
		fd_set wfds;
		struct timeval timeout;

		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_SET(fd->fd, &wfds);

		(void)select(fd->fd + 1, NULL, &wfds, NULL, &timeout);
	}
}

#if defined(HAVE_PSELECT)
static void stress_fd_pselect_rdwr(stress_fd_t *fd)
{
	if ((fd->fd >= 0) && (fd->fd < FD_SETSIZE)) {
		struct timespec tv;
		fd_set rfds, wfds;

		tv.tv_sec = 0;
		tv.tv_nsec = 0;
		FD_ZERO(&rfds);
		FD_SET(fd->fd, &rfds);
		FD_ZERO(&wfds);
		FD_SET(fd->fd, &wfds);

		(void)pselect(fd->fd + 1, &rfds, &wfds, NULL, &tv, NULL);
	}
}
#endif

#if defined(POLLIN) &&	\
    defined(POLLOUT)
static void stress_fd_poll_rdwr(stress_fd_t *fd)
{
	struct pollfd fds[1];

	fds[0].fd = fd->fd;
	fds[0].events = POLLIN | POLLOUT;
	fds[0].revents = 0;

	(void)poll(fds, 1, 0);
}
#endif

#if defined(HAVE_PPOLL) &&	\
    defined(POLLIN) &&		\
    defined(POLLOUT)
static void stress_fd_ppoll_rdwr(stress_fd_t *fd)
{
	struct timespec tv;
	struct pollfd fds[1];

	tv.tv_sec = 0;
	tv.tv_nsec = 0;
	fds[0].fd = fd->fd;
	fds[0].events = POLLIN | POLLOUT;
	fds[0].revents = 0;

	(void)ppoll(fds, 1, &tv, NULL);
}
#endif

static void stress_fd_mmap_rd(stress_fd_t *fd)
{
	void *ptr;

	ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd->fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
}

static void stress_fd_mmap_wr(stress_fd_t *fd)
{
	void *ptr;

	ptr = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd->fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
}

#if defined(IN_MASK_CREATE) &&  \
    defined(IN_MASK_ADD)
static void stress_fd_inotify_add_watch(stress_fd_t *fd)
{
	int wd;

	wd = inotify_add_watch(fd->fd, "inotify_file", IN_MASK_CREATE | IN_MASK_ADD);
	if (wd >= 0)
		(void)inotify_rm_watch(fd->fd, wd);
}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
static void stress_fd_timerfd_gettime(stress_fd_t *fd)
{
	struct itimerspec value;

	(void)timerfd_gettime(fd->fd, &value);
}
#endif

static void stress_fd_pidfd_send_signal(stress_fd_t *fd)
{
	(void)shim_pidfd_send_signal(fd->fd, 0, NULL, 0);
}

#if defined(FIOQSIZE)
static void stress_fd_ioctl_fioqsize(stress_fd_t *fd)
{
	shim_loff_t sz;

	VOID_RET(int, ioctl(fd->fd, FIOQSIZE, &sz));
}
#endif


#if defined(__NR_getdents)
static void stress_fd_getdents(stress_fd_t *fd)
{
	char buffer[8192];

	(void)syscall(__NR_getdents, fd->fd, buffer, sizeof(buffer));
}
#endif

static void stress_fd_fstat(stress_fd_t *fd)
{
	struct stat statbuf;

	(void)fstat(fd->fd, &statbuf);
}

#if defined(F_GETFL)
static void stress_fd_fcntl_f_getfl(stress_fd_t *fd)
{
	(void)fcntl(fd->fd, F_GETFL);
}
#endif

static void stress_fd_ftruncate(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 10.0))
		VOID_RET(int, ftruncate(fd->fd, 0));
}

#if defined(POSIX_FADV_RANDOM) &&	\
    defined(HAVE_POSIX_FADVISE)
static void stress_fd_posix_fadvise(stress_fd_t *fd)
{
	(void)posix_fadvise(fd->fd, 0, 0, POSIX_FADV_RANDOM);
	(void)posix_fadvise(fd->fd, 0, 1024, POSIX_FADV_RANDOM);
	(void)posix_fadvise(fd->fd, 1024, 0, POSIX_FADV_RANDOM);
}
#endif

static void stress_fd_listen(stress_fd_t *fd)
{
	(void)listen(fd->fd, 0);
	(void)shutdown(fd->fd, SHUT_RDWR);
}

static void stress_fd_accept(stress_fd_t *fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);	/* invalid */

	struct stat statbuf;

	if (fstat(fd->fd, &statbuf) < 0)
		return;

	/* don't accept on sockets! */
	if ((statbuf.st_mode & S_IFMT) == S_IFSOCK)
		return;

	(void)shim_memset(&addr, 0x00, sizeof(addr));
	(void)accept(fd->fd, &addr, &addrlen);
}

static void stress_fd_shutdown(stress_fd_t *fd)
{
	(void)shutdown(fd->fd, SHUT_RDWR);
}

static void stress_fd_getsockname(stress_fd_t *fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);

	(void)shim_memset(&addr, 0, sizeof(addr));
	(void)getsockname(fd->fd, &addr, &addrlen);
}

static void stress_fd_getpeername(stress_fd_t *fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);

	(void)shim_memset(&addr, 0, sizeof(addr));
	(void)getpeername(fd->fd, &addr, &addrlen);
}

#if defined(HAVE_SYNCFS)
static void stress_fd_syncfs(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 29.0))
		(void)syncfs(fd->fd);
}
#endif

#if defined(HAVE_FDATASYNC)
static void stress_fd_fdatasync(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 31.0))
		(void)shim_fdatasync(fd->fd);
}
#endif

static void stress_fd_fsync(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 37.0))
		(void)shim_fsync(fd->fd);
}

static void stress_fd_fchdir(stress_fd_t *fd)
{
	char mycwd[PATH_MAX];

	if (!getcwd(mycwd, sizeof(mycwd)))
		return;
	if (fchdir(fd->fd) < 0)
		return;
	VOID_RET(int, chdir(mycwd));
}

static void stress_fd_chmod(stress_fd_t *fd)
{
	struct stat statbuf;
	static double t = 0.0;

	if (stress_fd_now(&t, 1.0)) {
		if (fstat(fd->fd, &statbuf) < 0)
			return;
		(void)fchmod(fd->fd, statbuf.st_mode);
	}
}

#if defined(HAVE_SYS_STATVFS_H)
static void stress_fd_fstatfs(stress_fd_t *fd)
{
	struct statvfs buf;

	(void)fstatvfs(fd->fd, &buf);
}
#endif

#if defined(HAVE_FUTIMENS)
static void stress_fd_futimens(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 1.0)) {
		/* set time to now */
		(void)futimens(fd->fd, NULL);
	}
}
#endif

#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_EX) &&         \
    defined(LOCK_UN)
static void stress_fd_flock(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 11.0)) {
		if (flock(fd->fd, LOCK_EX) < 0)
			return;
		(void)flock(fd->fd, LOCK_UN);
	}
}
#endif

#if defined(F_DUPFD)
static void stress_fd_fcntl_f_dupfd(stress_fd_t *fd)
{
	int fd2;

	fd2 = fcntl(fd->fd, F_DUPFD, stress_mwc16() + 100);
	if (fd2 >= 0)
		(void)close(fd2);
}
#endif

#if defined(F_NOTIFY) &&	\
    defined(DN_ACCESS)
static void stress_fd_fnctl_f_notify(stress_fd_t *fd)
{
	(void)fcntl(fd->fd, F_NOTIFY, DN_ACCESS);
	(void)fcntl(fd->fd, F_NOTIFY, 0);
}
#endif

#if defined(F_SETFL)
static void stress_fd_fcntl_f_setfl(stress_fd_t *fd)
{
	static const int flags[] = {
		0,
#if defined(O_APPEND)
		O_APPEND,
#endif
#if defined(O_ASYNC)
		O_ASYNC,
#endif
#if defined(O_DIRECT)
		O_DIRECT,
#endif
#if defined(O_NOATIME)
		O_NOATIME,
#endif
#if defined(O_NONBLOCK)
		O_NONBLOCK,
#endif
#if defined(O_DSYNC)
		O_DSYNC,	/* linux silently ignored */
#endif
#if defined(O_SYNC)
		O_SYNC,		/* linux silently ignored */
#endif
	};

	const int new_flag = flags[stress_mwc8modn(SIZEOF_ARRAY(flags))];
	int old_flag;

	old_flag = fcntl(fd->fd, F_GETFL);
	if (old_flag < 0)
		return;
	(void)fcntl(fd->fd, F_SETFL, old_flag | new_flag);
	(void)fcntl(fd->fd, F_SETFL, old_flag & ~new_flag);
	(void)fcntl(fd->fd, F_SETFL, old_flag);
}
#endif

#if defined(F_GETOWN)
static void stress_fd_fcntl_f_getown(stress_fd_t *fd)
{
	(void)fcntl(fd->fd, F_GETOWN);
}
#endif

#if defined(F_SETPIPE_SZ)
static void stress_fd_fctnl_f_setpipe_sz(stress_fd_t *fd)
{
	(void)fcntl(fd->fd, F_SETPIPE_SZ, 1024);	/* Illegal */
	(void)fcntl(fd->fd, F_SETPIPE_SZ, 4096 * (stress_mwc8() & 31));
}
#endif

#if defined(F_SET_RW_HINT)
static void stress_fd_fcntl_f_set_rw_hint(stress_fd_t *fd)
{
	static const uint64_t hints[] = {
		0,
#if defined(RWH_WRITE_LIFE_NOT_SET)
		RWH_WRITE_LIFE_NOT_SET,
#endif
#if defined(RWH_WRITE_LIFE_NONE)
		RWH_WRITE_LIFE_NONE,
#endif
#if defined(RWH_WRITE_LIFE_SHORT)
		RWH_WRITE_LIFE_SHORT,
#endif
#if defined(RWH_WRITE_LIFE_MEDIUM)
		RWH_WRITE_LIFE_MEDIUM,
#endif
#if defined(RWH_WRITE_LIFE_LONG)
		RWH_WRITE_LIFE_LONG,
#endif
#if defined(RWH_WRITE_LIFE_LONG)
		RWH_WRITE_LIFE_EXTREME,
#endif
	};
	uint64_t hint;

	hint = hints[stress_mwc8modn(SIZEOF_ARRAY(hints))];
	(void)fcntl(fd->fd, F_SET_RW_HINT, &hint);
	hint = hints[stress_mwc8modn(SIZEOF_ARRAY(hints))];
	(void)fcntl(fd->fd, F_SET_FILE_RW_HINT, &hint);
}
#endif

#if defined(F_SETLEASE) &&	\
    defined(F_RDLCK) &&		\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
static void stress_fd_fcntl_f_setlease(stress_fd_t *fd)
{
	if (fcntl(fd->fd, F_SETLEASE, stress_mwc1() ? F_RDLCK : F_WRLCK) < 0)
		return;
	(void)fcntl(fd->fd, F_SETLEASE, F_UNLCK);
}
#endif

#if defined(HAVE_WAITID)
static void stress_fd_waitid(stress_fd_t *fd)
{
	siginfo_t info;

	(void)shim_memset(&info, 0, sizeof(info));
	(void)waitid((idtype_t)P_PIDFD, (id_t)fd->fd, &info, WNOHANG);
}
#endif

#if defined(HAVE_SETNS)
static void stress_fd_setns(stress_fd_t *fd)
{
	(void)setns(fd->fd, 0);
}
#endif

#if defined(HAVE_LOCKF) &&	\
    defined(F_TLOCK) &&		\
    defined(F_UNLOCK)
static void stress_fd_lockf(stress_fd_t *fd)
{
	static double t = 0.0;

	if (stress_fd_now(&t, 13.0)) {
		if (lockf(fd->fd, F_TLOCK, 0) < 0)
			return;
		(void)lockf(fd->fd, F_UNLOCK);
	}
}
#endif

#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_FLISTXATTR)
static void stress_fd_flistxattr(stress_fd_t *fd)
{
	char buffer[4096];

	(void)shim_flistxattr(fd->fd, buffer, sizeof(buffer));
}
#endif

#if defined(HAVE_VMSPLICE) &&	\
    defined(SPLICE_F_NONBLOCK)
static void stress_fd_vmslice(stress_fd_t *fd)
{
	struct iovec iov[1];
	const size_t sz = 4096;
	void *ptr;

	ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED)
		return;

	iov[0].iov_base = ptr;
	iov[0].iov_len = sz;

	(void)vmsplice(fd->fd, iov, 1, SPLICE_F_NONBLOCK);
	(void)munmap(ptr, sz);
}
#endif

static void stress_fd_read(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_READ) {
		const off_t offset = (off_t)stress_mwc32();

		if (offset == lseek(fd->fd, offset, SEEK_SET)) {
			char data[16];

			VOID_RET(ssize_t, read(fd->fd, data, sizeof(data)));
		}
	}
}

static void stress_fd_write(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		const off_t offset = (off_t)stress_mwc16();

		if (offset == lseek(fd->fd, offset, SEEK_SET)) {
			char data[16];

			stress_rndbuf(data, sizeof(data));
			VOID_RET(ssize_t, write(fd->fd, data, sizeof(data)));
		}
	}
}

#if defined(HAVE_PREAD)
static void stress_fd_pread(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_READ) {
		const off_t offset = (off_t)stress_mwc32();
		char data[16];

		VOID_RET(ssize_t, pread(fd->fd, data, sizeof(data), offset));
	}
}
#endif

#if defined(HAVE_PWRITE)
static void stress_fd_pwrite(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		const off_t offset = (off_t)stress_mwc16();
		char data[16];

		stress_rndbuf(data, sizeof(data));
		VOID_RET(ssize_t, pwrite(fd->fd, data, sizeof(data), offset));
	}
}
#endif

#if defined(HAVE_READV)
static void stress_fd_readv(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_READ) {
		const off_t offset = (off_t)stress_mwc32();

		if (offset == lseek(fd->fd, offset, SEEK_SET)) {
			struct iovec iov[1];
			char data[16];

			iov[0].iov_base = data;
			iov[0].iov_len = sizeof(data);

			VOID_RET(ssize_t, readv(fd->fd, iov, 1));
		}
	}
}
#endif

#if defined(HAVE_WRITEV)
static void stress_fd_writev(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		const off_t offset = (off_t)stress_mwc16();

		if (offset == lseek(fd->fd, offset, SEEK_SET)) {
			struct iovec iov[1];
			char data[16];

			iov[0].iov_base = data;
			iov[0].iov_len = sizeof(data);
			stress_rndbuf(data, sizeof(data));
			VOID_RET(ssize_t, writev(fd->fd, iov, 1));
		}
	}
}
#endif

#if defined(HAVE_PREADV)
static void stress_fd_preadv(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_READ) {
		struct iovec iov[1];
		const off_t offset = (off_t)stress_mwc32();
		char data[16];

		iov[0].iov_base = data;
		iov[0].iov_len = sizeof(data);

		VOID_RET(ssize_t, preadv(fd->fd, iov, 1, offset));
	}
}
#endif

#if defined(HAVE_PWRITEV)
static void stress_fd_pwritev(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		struct iovec iov[1];
		const off_t offset = (off_t)stress_mwc16();
		char data[16];

		iov[0].iov_base = data;
		iov[0].iov_len = sizeof(data);
		stress_rndbuf(data, sizeof(data));
		VOID_RET(ssize_t, pwritev(fd->fd, iov, 1, offset));
	}
}
#endif

#if defined(HAVE_PREADV2) ||	\
    defined(HAVE_PWRITEV2)
static const int rwf_flags[] = {
	0,
#if defined(RWF_DSYNC)
	RWF_DSYNC,
#endif
#if defined(RWF_HIPRI)
	RWF_HIPRI,
#endif
#if defined(RWF_SYNC)
	RWF_SYNC,
#endif
#if defined(RWF_NOWAIT)
	RWF_NOWAIT,
#endif
#if defined(RWF_APPEND)
	RWF_APPEND,
#endif
};
#endif

#if defined(HAVE_PREADV2)
static void stress_fd_preadv2(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_READ) {
		struct iovec iov[1];
		const off_t offset = (off_t)stress_mwc32();
		const int flag = rwf_flags[stress_mwc8modn(SIZEOF_ARRAY(rwf_flags))];
		char data[16];

		iov[0].iov_base = data;
		iov[0].iov_len = sizeof(data);

		VOID_RET(ssize_t, preadv2(fd->fd, iov, 1, offset, flag));
	}
}
#endif

#if defined(HAVE_PWRITEV2)
static void stress_fd_pwritev2(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		struct iovec iov[1];
		const off_t offset = (off_t)stress_mwc16();
		const int flag = rwf_flags[stress_mwc8modn(SIZEOF_ARRAY(rwf_flags))];
		char data[16];

		iov[0].iov_base = data;
		iov[0].iov_len = sizeof(data);
		stress_rndbuf(data, sizeof(data));
		VOID_RET(ssize_t, pwritev2(fd->fd, iov, 1, offset, flag));
	}
}
#endif

#if defined(MSG_DONTWAIT)
static void stress_fd_recv(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_RECV | FD_FLAG_READ)) {
		char buf[16];

		VOID_RET(ssize_t, recv(fd->fd, buf, sizeof(buf), MSG_DONTWAIT));
	}
}
#endif

#if defined(MSG_DONTWAIT)
static void stress_fd_send(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_SEND | FD_FLAG_WRITE)) {
		char buf[1];

		buf[0] = stress_mwc8();

		VOID_RET(ssize_t, send(fd->fd, buf, 0, MSG_DONTWAIT));
	}
}
#endif

#if defined(MSG_DONTWAIT)
static void stress_fd_recvfrom(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_RECV | FD_FLAG_READ)) {
		char buf[16];

		VOID_RET(ssize_t, recvfrom(fd->fd, buf, sizeof(buf), MSG_DONTWAIT, NULL, 0));
	}
}
#endif

#if defined(MSG_DONTWAIT)
static void stress_fd_sendto(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_SEND | FD_FLAG_WRITE)) {
		char buf[1];

		buf[0] = stress_mwc8();

		VOID_RET(ssize_t, sendto(fd->fd, buf, 0, MSG_DONTWAIT, NULL, 0));
	}
}
#endif

#if defined(MSG_DONTWAIT) &&	\
    defined(HAVE_RECVMSG)
static void stress_fd_recvmsg(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_RECV | FD_FLAG_READ)) {
		struct iovec iov[1];
		struct msghdr m;
		char buf[16];

		iov[0].iov_base = buf;
		iov[0].iov_len = sizeof(buf);

		(void)shim_memset(&m, 0, sizeof(m));
		m.msg_iov = iov;
		m.msg_iovlen = 1;

		VOID_RET(ssize_t, recvmsg(fd->fd, &m, MSG_DONTWAIT));
	}
}
#endif

#if defined(MSG_DONTWAIT) &&	\
    defined(HAVE_SENDMSG)
static void stress_fd_sendmsg(stress_fd_t *fd)
{
	if (fd->flags & (FD_FLAG_SEND | FD_FLAG_WRITE)) {
		struct iovec iov[1];
		struct msghdr m;
		char buf[1];

		buf[0] = stress_mwc8();
		iov[0].iov_base = buf;
		iov[0].iov_len = 0;

		(void)shim_memset(&m, 0, sizeof(m));
		m.msg_iov = iov;
		m.msg_iovlen = 1;

		/* intentionally bogus sendmsg call */
		VOID_RET(ssize_t, sendmsg(fd->fd, &m, MSG_DONTWAIT));
	}
}
#endif

#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE)
static void stress_fd_sendfile(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		int fd_in;

		fd_in = open("/dev/zero", O_RDONLY);
		if (fd_in >= 0) {
			off_t offset = 0;

			VOID_RET(ssize_t, sendfile(fd->fd, fd_in, &offset, 16));
			(void)close(fd_in);
		}
	}
}
#endif

#if defined(HAVE_COPY_FILE_RANGE)
static void stress_fd_copy_file_range(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		int fd_in;

		fd_in = open(stress_fd_filename, O_RDONLY);
		if (fd_in >= 0) {
			off_t off_in, off_out;

			off_in = 0;
			off_out = 4096;
			VOID_RET(ssize_t, shim_copy_file_range(fd_in, &off_in, fd->fd, &off_out, 4096, 0));
			(void)close(fd_in);
		}
	}
}
#endif

#if defined(HAVE_SPLICE)
static void stress_fd_splice(stress_fd_t *fd)
{
	if (fd->flags & FD_FLAG_WRITE) {
		int fd_in;

		fd_in = open("/dev/zero", O_RDONLY);
		if (fd_in >= 0) {
			off_t off_in, off_out;

			off_in = 0;
			off_out = 0;
			/* Exercise -ESPIPE errors */
			VOID_RET(ssize_t, splice(fd_in, &off_in, fd->fd, &off_out, 4096, 0));
			(void)close(fd_in);
		}
	}
}
#endif

static const fd_func_t fd_funcs[] = {
#if defined(SOL_SOCKET)
	stress_fd_sockopt_reuseaddr,
#endif
	stress_fd_lseek,
	stress_fd_dup,
	stress_fd_dup2,
#if defined(O_CLOEXEC)
	stress_fd_dup3,
#endif
	stress_fd_bind_af_inet,
#if defined(AF_INET6)
	stress_fd_bind_af_inet6,
#endif
	stress_fd_select_rd,
	stress_fd_select_wr,
#if defined(HAVE_PSELECT)
	stress_fd_pselect_rdwr,
#endif
#if defined(POLLIN) &&	\
    defined(POLLOUT)
	stress_fd_poll_rdwr,
#endif
#if defined(HAVE_PPOLL) &&	\
    defined(POLLIN) &&		\
    defined(POLLOUT)
	stress_fd_ppoll_rdwr,
#endif
	stress_fd_mmap_rd,
	stress_fd_mmap_wr,
#if defined(IN_MASK_CREATE) &&  \
    defined(IN_MASK_ADD)
	stress_fd_inotify_add_watch,
#endif
#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
	stress_fd_timerfd_gettime,
#endif
	stress_fd_pidfd_send_signal,
#if defined(FIOQSIZE)
	stress_fd_ioctl_fioqsize,
#endif
#if defined(__NR_getdents)
	stress_fd_getdents,
#endif
	stress_fd_fstat,
#if defined(F_GETFL)
	stress_fd_fcntl_f_getfl,
#endif
	stress_fd_ftruncate,
#if defined(POSIX_FADV_RANDOM) &&	\
    defined(HAVE_POSIX_FADVISE)
	stress_fd_posix_fadvise,
#endif
	stress_fd_listen,
	stress_fd_accept,
	stress_fd_shutdown,
	stress_fd_getsockname,
	stress_fd_getpeername,
#if defined(HAVE_SYNCFS)
	stress_fd_syncfs,
#endif
#if defined(HAVE_FDATASYNC)
	stress_fd_fdatasync,
#endif
	stress_fd_fsync,
	stress_fd_fchdir,
	stress_fd_chmod,
#if defined(HAVE_SYS_STATVFS_H)
	stress_fd_fstatfs,
#endif
#if defined(HAVE_FUTIMENS)
	stress_fd_futimens,
#endif
#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_EX) &&         \
    defined(LOCK_UN)
	stress_fd_flock,
#endif
#if defined(F_DUPFD)
	stress_fd_fcntl_f_dupfd,
#endif
#if defined(F_NOTIFY) &&	\
    defined(DN_ACCESS)
	stress_fd_fnctl_f_notify,
#endif
#if defined(F_SETFL)
	stress_fd_fcntl_f_setfl,
#endif
#if defined(F_GETOWN)
	stress_fd_fcntl_f_getown,
#endif
#if defined(F_SETPIPE_SZ)
	stress_fd_fctnl_f_setpipe_sz,
#endif
#if defined(F_SET_RW_HINT)
	stress_fd_fcntl_f_set_rw_hint,
#endif
#if defined(F_SETLEASE) &&	\
    defined(F_RDLCK) &&		\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
	stress_fd_fcntl_f_setlease,
#endif
#if defined(HAVE_WAITID)
	stress_fd_waitid,
#endif
#if defined(HAVE_SETNS)
	stress_fd_setns,
#endif
#if defined(HAVE_LOCKF) &&	\
    defined(F_TLOCK) &&		\
    defined(F_UNLOCK)
	stress_fd_lockf,
#endif
#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_FLISTXATTR)
	stress_fd_flistxattr,
#endif
#if defined(HAVE_VMSPLICE) &&	\
    defined(SPLICE_F_NONBLOCK)
	stress_fd_vmslice,
#endif
	stress_fd_read,
	stress_fd_write,
#if defined(HAVE_PREAD)
	stress_fd_pread,
#endif
#if defined(HAVE_PWRITE)
	stress_fd_pwrite,
#endif
#if defined(HAVE_READV)
	stress_fd_readv,
#endif
#if defined(HAVE_WRITEV)
	stress_fd_writev,
#endif
#if defined(HAVE_PREADV)
	stress_fd_preadv,
#endif
#if defined(HAVE_PWRITEV)
	stress_fd_pwritev,
#endif
#if defined(HAVE_PREADV2)
	stress_fd_preadv2,
#endif
#if defined(HAVE_PWRITEV2)
	stress_fd_pwritev2,
#endif
#if defined(MSG_DONTWAIT)
	stress_fd_recv,
#endif
#if defined(MSG_DONTWAIT)
	stress_fd_send,
#endif
#if defined(MSG_DONTWAIT)
	stress_fd_recvfrom,
#endif
#if defined(MSG_DONTWAIT)
	stress_fd_sendto,
#endif
#if defined(MSG_DONTWAIT) &&	\
    defined(HAVE_RECVMSG)
	stress_fd_recvmsg,
#endif
#if defined(MSG_DONTWAIT) &&	\
    defined(HAVE_SENDMSG)
	stress_fd_sendmsg,
#endif
#if defined(HAVE_SYS_SENDFILE_H) &&	\
    defined(HAVE_SENDFILE)
	stress_fd_sendfile,
#endif
#if defined(HAVE_COPY_FILE_RANGE)
	stress_fd_copy_file_range,
#endif
#if defined(HAVE_SPLICE)
	stress_fd_splice,
#endif
};

/*
 *  Handle and ignore SIGIO/SIGPIPE signals
 */
static void stress_fd_sig_handler(int sig)
{
	(void)sig;
}

static int stress_fd_abuse_process(stress_args_t *args, void *context)
{
	size_t i, n;
	pid_t pid;
	stress_fd_t fds[SIZEOF_ARRAY(open_funcs)];

	(void)context;

	for (i = 0, n = 0; i < SIZEOF_ARRAY(fds); i++) {
		open_funcs[i](&fds[i]);

		if (fds[i].fd < 0)
			continue;
		n++;
	}

	/*
	 *  Parent and child processes operated on same
	 *  fds for more of a stress mix.
	 */
	pid = fork();
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	do {
		size_t j;

		for (i = 0; stress_continue(args) && (i < n); i++) {
			t_now = stress_time_now();
			for (j = 0; stress_continue(args) && (j < SIZEOF_ARRAY(fd_funcs)); j++) {
				fd_funcs[j](&fds[i]);
				if (pid > -1)
					stress_bogo_inc(args);
			}
		}

		t_now = stress_time_now();
		for (i = 0 ; stress_continue(args) && (i < 20); i++) {
			register const size_t func_idx = stress_mwc8modn(SIZEOF_ARRAY(fd_funcs));
			register const size_t fd_idx = stress_mwc8modn(n);

			fd_funcs[func_idx](&fds[fd_idx]);
			if (pid > -1)
				stress_bogo_inc(args);
		}
	} while (stress_continue(args));

	if (pid < 0) {
		/* do nothing */
	} else if (pid == 0) {
		_exit(0);
	} else {
		(void)stress_kill_and_wait(args, pid, SIGKILL, false);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < n; i++)
		(void)close(fds[i].fd);

	if (*stress_fd_filename) {
		(void)shim_unlink(stress_fd_filename);
		(void)stress_temp_dir_rm_args(args);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
static int stress_fd_abuse(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;

#if defined(SIGIO)
	if (stress_sighandler(args->name, SIGIO, stress_fd_sig_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
#endif
	if (stress_sighandler(args->name, SIGPIPE, stress_fd_sig_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	if (stress_temp_dir_mk_args(args) < 0) {
		shim_memset(stress_fd_filename, 0, sizeof(stress_fd_filename));
	} else {
		(void)stress_temp_filename_args(args, stress_fd_filename,
			sizeof(stress_fd_filename), stress_mwc32());
	}

	if (stress_instance_zero(args))
		pr_dbg("%s: %zd fd opening operations, %zd fd exercising operations\n",
			args->name, SIZEOF_ARRAY(open_funcs), SIZEOF_ARRAY(fd_funcs));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, NULL, stress_fd_abuse_process, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_fd_abuse_info = {
	.stressor = stress_fd_abuse,
	.classifier = CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help
};
