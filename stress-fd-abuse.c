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

#include <sys/ioctl.h>
#include <sys/file.h>
#include <sched.h>

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

typedef int (*open_func_t)(void);
typedef void (*fd_func_t)(int fd);

char stress_fd_filename[PATH_MAX];

static const stress_help_t help[] = {
	{ NULL,	"fd-abuse N",	"start N workers abusing file descriptors" },
	{ NULL,	"fd-abuse N",	"stop fd-abuse after bogo operations" },
	{ NULL,	NULL,		NULL }
};

static int stress_fd_open_null(void)
{
	return open("/dev/null", O_RDWR);
}

static int stress_fd_open_zero(void)
{
	return open("/dev/zero", O_RDWR);
}

static int stress_fd_creat_file(void)
{
	if (*stress_fd_filename)
		return creat(stress_fd_filename, S_IRUSR | S_IWUSR);
	return -1;
}

static int stress_fd_open_file_ro(void)
{
	return open(stress_fd_filename, O_RDONLY);
}

static int stress_fd_open_file_wo(void)
{
	return open(stress_fd_filename, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
}

static int stress_fd_open_file_rw(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
}

static int stress_fd_open_file_noaccess(void)
{
	/* Linux allows this for ioctls, O_NOACCESS */
	return open(stress_fd_filename, O_WRONLY | O_RDWR | O_APPEND, S_IRUSR | S_IWUSR);
}

#if defined(O_ASYNC)
static int stress_fd_open_file_rw_async(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_ASYNC, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_DIRECT)
static int stress_fd_open_file_rw_direct(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_DIRECT, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_NONBLOCK) &&	\
    defined(O_DIRECTORY)
static int stress_fd_open_temp_path(void)
{
	const char *tmp = stress_get_temp_path();

	if (tmp)
		return openat(AT_FDCWD, tmp, O_RDONLY | O_NONBLOCK | O_DIRECTORY);
	return -1;
}
#endif

#if defined(O_DSYNC)
static int stress_fd_open_file_rw_dsync(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_DSYNC, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_LARGEFILE)
static int stress_fd_open_file_rw_largefile(void)
{
	if (O_LARGEFILE)
		return open(stress_fd_filename, O_RDWR | O_APPEND | O_LARGEFILE, S_IRUSR | S_IWUSR);
	return -1;
}
#endif

#if defined(O_NOATIME)
static int stress_fd_open_file_rw_noatime(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_NOATIME, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_NONBLOCK)
static int stress_fd_open_file_rw_nonblock(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_NONBLOCK, S_IRUSR | S_IWUSR);
}
#endif

#if defined(O_PATH)
static int stress_fd_open_file_path(void)
{
	const char *tmp = stress_get_temp_path();

	if (tmp)
		return open(tmp, O_PATH);
	return -1;
}
#endif

#if defined(O_SYNC)
static int stress_fd_open_file_rw_sync(void)
{
	return open(stress_fd_filename, O_RDWR | O_APPEND | O_SYNC, S_IRUSR | S_IWUSR);
}
#endif

static int stress_fd_open_pipe_rd_end(void)
{
	int fds[2];

	if (pipe(fds) < 0)
		return -1;
	(void)close(fds[1]);
	return fds[0];
}

static int stress_fd_open_pipe_wr_end(void)
{
	int fds[2];

	if (pipe(fds) < 0)
		return -1;
	(void)close(fds[0]);
	return fds[1];
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
static int stress_fd_open_pipe2_rd_end(void)
{
	int fds[2];
	const int flag = pipe2_flags[stress_mwc8modn(SIZEOF_ARRAY(pipe2_flags))];

	if (pipe2(fds, flag) < 0)
		return -1;
	(void)close(fds[1]);
	return fds[0];
}
#endif

#if defined(HAVE_PIPE2)
static int stress_fd_open_pipe2_wr_end(void)
{
	int fds[2];
	const int flag = pipe2_flags[stress_mwc8modn(SIZEOF_ARRAY(pipe2_flags))];

	if (pipe2(fds, flag) < 0)
		return -1;
	(void)close(fds[0]);
	return fds[1];
}
#endif

#if defined(HAVE_EVENTFD) &&	\
    defined(HAVE_SYS_EVENTFD_H)
static int stress_fd_open_eventfd(void)
{
	return eventfd(0, 0);
}
#endif

#if defined(HAVE_MEMFD_CREATE)
static int stress_fd_open_memfd(void)
{
	char name[64];

	(void)snprintf(name, sizeof(name), "memfd-%" PRIdMAX "-%" PRIu32,
		(intmax_t)getpid(), stress_mwc32());
	return shim_memfd_create(name, 0);
}
#endif

#if defined(__NR_memfd_secret)
static int stress_fd_open_memfd_secret(void)
{
	return shim_memfd_secret(0);
}
#endif

#if defined(AF_INET) &&	\
    defined(SOCK_STREAM)
static int stress_fd_open_sock_inet_stream(void)
{
	return socket(AF_INET, SOCK_STREAM, 0);
}
#endif

#if defined(AF_INET6) &&	\
    defined(SOCK_STREAM)
static int stress_fd_open_sock_inet6_stream(void)
{
	return socket(AF_INET6, SOCK_STREAM, 0);
}
#endif

#if defined(AF_INET) &&	\
    defined(SOCK_DGRAM)
static int stress_fd_open_sock_inet_dgram(void)
{
	return socket(AF_INET, SOCK_DGRAM, 0);
}
#endif

#if defined(AF_INET6) &&	\
    defined(SOCK_DGRAM)
static int stress_fd_open_sock_inet6_dgram(void)
{
	return socket(AF_INET6, SOCK_DGRAM, 0);
}
#endif

#if defined(AF_UNIX) &&	\
    defined(SOCK_STREAM)
static int stress_fd_open_sock_af_unix_stream(void)
{
	return socket(AF_UNIX, SOCK_STREAM, 0);
}
#endif

#if defined(AF_UNIX) &&	\
    defined(SOCK_DGRAM)
static int stress_fd_open_sock_af_unix_dgram(void)
{
	return socket(AF_UNIX, SOCK_DGRAM, 0);
}
#endif

#if defined(AF_ALG) &&	\
    defined(SOCK_SEQPACKET)
static int stress_fd_open_sock_af_alg_seqpacket(void)
{
	return socket(AF_ALG, SOCK_SEQPACKET, 0);
}
#endif

#if defined(AF_INET) &&		\
    defined(SOCK_DGRAM) &&	\
    defined(IPPROTO_ICMP)
static int stress_fd_open_sock_af_inet_dgram_icmp(void)
{
	return socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP);
}
#endif

static int stress_fd_open_socketpair(void)
{
	int sv[2];

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0)
		return -1;

	(void)close(sv[1]);
	return sv[0];

}

#if defined(O_TMPFILE)
static int stress_fd_open_tmpfile(void)
{
	return open("/tmp", O_TMPFILE | O_RDWR, S_IRUSR | S_IWUSR);
}
#endif

#if defined(HAVE_USERFAULTFD)
static int stress_fd_open_userfaultfd(void)
{
	return shim_userfaultfd(0);
}
#endif

#if defined(HAVE_SYS_INOTIFY_H)
static int stress_fd_open_inotify_init(void)
{
	return inotify_init();
}
#endif

#if defined(HAVE_PTSNAME)
static int stress_fd_open_ptxm(void)
{
	return open("/dev/ptmx", O_RDWR);
}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
static int stress_fd_open_timerfd(void)
{
	return timerfd_create(CLOCK_REALTIME, 0);
}
#endif

#if defined(HAVE_PIDFD_OPEN)
static int stress_fd_open_pidfd(void)
{
	return shim_pidfd_open(getpid(), 0);
}
#endif

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE)
static int stress_fd_open_epoll_create(void)
{
	return epoll_create(1);
}
#endif

static open_func_t open_funcs[] = {
	stress_get_bad_fd,
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
    defined(O_DIRECTORY)
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

static void stress_fd_sockopt_reuseaddr(int fd)
{
	int so_reuseaddr = 1;

	(void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                &so_reuseaddr, sizeof(so_reuseaddr));
}

static void stress_fd_lseek(int fd)
{
	VOID_RET(off_t, lseek(fd, 0, SEEK_SET));
	VOID_RET(off_t, lseek(fd, 0, SEEK_END));
	VOID_RET(off_t, lseek(fd, 0, SEEK_CUR));
	VOID_RET(off_t, lseek(fd, 999, SEEK_SET));
	VOID_RET(off_t, lseek(fd, 999, SEEK_END));
	VOID_RET(off_t, lseek(fd, 999, SEEK_CUR));
}

static void stress_fd_dup(int fd)
{
	int fd2;

	fd2 = dup(fd);
	if (fd2 >= 0)
		(void)close(fd2);
}

static void stress_fd_dup2(int fd)
{
	int fd2;

	fd2 = dup2(fd, stress_mwc16() + 100);
	if (fd2 >= 0)
		(void)close(fd2);
}

#if defined(O_CLOEXEC)
static void stress_fd_dup3(int fd)
{
	int fd2;

	fd2 = shim_dup3(fd, stress_mwc16() + 100, O_CLOEXEC);
	if (fd2 >= 0)
		(void)close(fd2);
}
#endif

static void stress_fd_bind_af_inet(int fd)
{
	struct sockaddr_in addr;
	int newfd;

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_family = (sa_family_t)AF_INET;
	addr.sin_port = htons(40000);
	newfd = bind(fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr));
	if (newfd >= 0) {
		(void)close(newfd);
		(void)shutdown(fd, SHUT_RDWR);
	}
	(void)shutdown(fd, SHUT_RDWR);
}

static void stress_fd_bind_af_inet6(int fd)
{
	struct sockaddr_in6 addr;
	int newfd;
#if defined(__minix__)
	struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
	struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
#endif

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin6_addr = in6addr_loopback;
	addr.sin6_family = (sa_family_t)AF_INET6;
	addr.sin6_port = htons(40000);
	newfd = bind(fd, (const struct sockaddr *)&addr, (socklen_t)sizeof(addr));
	if (newfd >= 0) {
		(void)shutdown(newfd, SHUT_RDWR);
		(void)close(newfd);
	}
	(void)shutdown(fd, SHUT_RDWR);
}

static void stress_fd_select_rd(int fd)
{
	if ((fd >= 0) && (fd < FD_SETSIZE)) {
		fd_set rfds;
		struct timeval timeout;

		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);

		(void)select(fd + 1, &rfds, NULL, NULL, &timeout);
	}
}

static void stress_fd_select_wr(int fd)
{
	if ((fd >= 0) && (fd < FD_SETSIZE)) {
		fd_set wfds;
		struct timeval timeout;

		timeout.tv_sec = 0;
		timeout.tv_usec = 0;
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);

		(void)select(fd + 1, NULL, &wfds, NULL, &timeout);
	}
}

#if defined(HAVE_PSELECT)
static void stress_fd_pselect_rdwr(int fd)
{
	if ((fd >= 0) && (fd < FD_SETSIZE)) {
		struct timespec tv;
		fd_set rfds, wfds;

		tv.tv_sec = 0;
		tv.tv_nsec = 0;
		FD_ZERO(&rfds);
		FD_SET(fd, &rfds);
		FD_ZERO(&wfds);
		FD_SET(fd, &wfds);

		(void)pselect(fd + 1, &rfds, &wfds, NULL, &tv, NULL);
	}
}
#endif

static void stress_fd_poll_rdwr(int fd)
{
	struct pollfd fds[1];

	fds[0].fd = fd;
	fds[0].events = POLLIN | POLLOUT;
	fds[0].revents = 0;

	(void)poll(fds, 1, 0);
}

#if defined(HAVE_PPOLL)
static void stress_fd_ppoll_rdwr(int fd)
{
	struct timespec tv;
	struct pollfd fds[1];

	tv.tv_sec = 0;
	tv.tv_nsec = 0;
	fds[0].fd = fd;
	fds[0].events = POLLIN | POLLOUT;
	fds[0].revents = 0;

	(void)ppoll(fds, 1, &tv, NULL);
}
#endif

static void stress_fd_mmap_rd(int fd)
{
	void *ptr;

	ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
}

static void stress_fd_mmap_wr(int fd)
{
	void *ptr;

	ptr = mmap(NULL, 4096, PROT_WRITE, MAP_SHARED, fd, 0);
	if (ptr != MAP_FAILED)
		(void)munmap(ptr, 4096);
}

#if defined(IN_MASK_CREATE) &&  \
    defined(IN_MASK_ADD)
static void stress_fd_inotify_add_watch(int fd)
{
	int wd;

	wd = inotify_add_watch(fd, "inotify_file", IN_MASK_CREATE | IN_MASK_ADD);
	if (wd >= 0)
		(void)inotify_rm_watch(fd, wd);
}
#endif

#if defined(HAVE_SYS_TIMERFD_H) &&	\
    defined(HAVE_TIMERFD_CREATE) &&	\
    defined(HAVE_TIMERFD_GETTIME) &&	\
    defined(HAVE_TIMERFD_SETTIME) &&	\
    defined(CLOCK_REALTIME)
static void stress_fd_timerfd_gettime(int fd)
{
	struct itimerspec value;

	(void)timerfd_gettime(fd, &value);
}
#endif

static void stress_fd_pidfd_send_signal(int fd)
{
	(void)shim_pidfd_send_signal(fd, 0, NULL, 0);
}

#if defined(FIOQSIZE)
static void stress_fd_ioctl_fioqsize(int fd)
{
	shim_loff_t sz;

	VOID_RET(int, ioctl(fd, FIOQSIZE, &sz));
}
#endif


#if defined(__NR_getdents)
static void stress_fd_getdents(int fd)
{
	char buffer[8192];

	(void)syscall(__NR_getdents, fd, buffer, sizeof(buffer));
}
#endif

static void stress_fd_fstat(int fd)
{
	struct stat statbuf;

	(void)fstat(fd, &statbuf);
}

#if defined(F_GETFL)
static void stress_fd_fcntl_f_getfl(int fd)
{
	(void)fcntl(fd, F_GETFL);
}
#endif

static void stress_fd_ftruncate(int fd)
{
	VOID_RET(int, ftruncate(fd, 0));
}

#if defined(POSIX_FADV_RANDOM) ||	\
    defined(HAVE_POSIX_FADVISE)
static void stress_fd_posix_fadvise(int fd)
{
	(void)posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM);
	(void)posix_fadvise(fd, 0, 1024, POSIX_FADV_RANDOM);
	(void)posix_fadvise(fd, 1024, 0, POSIX_FADV_RANDOM);
}
#endif

static void stress_fd_listen(int fd)
{
	(void)listen(fd, 0);
	(void)shutdown(fd, SHUT_RDWR);
}

static void stress_fd_accept(int fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);	/* invalid */

	struct stat statbuf;

	if (fstat(fd, &statbuf) < 0)
		return;

	/* don't accept on sockets! */
	if ((statbuf.st_mode & S_IFMT) == S_IFSOCK)
		return;

	(void)shim_memset(&addr, 0x00, sizeof(addr));
	(void)accept(fd, &addr, &addrlen);
}

static void stress_fd_shutdown(int fd)
{
	(void)shutdown(fd, SHUT_RDWR);
}

static void stress_fd_getsockname(int fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);

	(void)shim_memset(&addr, 0, sizeof(addr));
	(void)getsockname(fd, &addr, &addrlen);
}

static void stress_fd_getpeername(int fd)
{
	struct sockaddr addr;
	socklen_t addrlen = sizeof(addr);

	(void)shim_memset(&addr, 0, sizeof(addr));
	(void)getpeername(fd, &addr, &addrlen);
}

#if defined(HAVE_SYNCFS)
static void stress_fd_syncfs(int fd)
{
	(void)syncfs(fd);
}
#endif

#if defined(HAVE_FDATASYNC)
static void stress_fd_fdatasync(int fd)
{
	(void)shim_fdatasync(fd);
}
#endif

static void stress_fd_fsync(int fd)
{
	(void)shim_fsync(fd);
}

static void stress_fd_fchdir(int fd)
{
	char mycwd[PATH_MAX];

	if (!getcwd(mycwd, sizeof(mycwd)))
		return;
	if (fchdir(fd) < 0)
		return;
	VOID_RET(int, chdir(mycwd));
}

static void stress_fd_chmod(int fd)
{
	struct stat statbuf;

	if (fstat(fd, &statbuf) < 0)
		return;
	(void)fchmod(fd, statbuf.st_mode);
}

#if defined(HAVE_SYS_STATVFS_H)
static void stress_fd_fstatfs(int fd)
{
	struct statvfs buf;

	(void)fstatvfs(fd, &buf);
}
#endif

#if defined(HAVE_FUTIMENS)
static void stress_fd_futimens(int fd)
{
	/* time time to now */
	(void)futimens(fd, NULL);
}
#endif

#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_EX) &&         \
    defined(LOCK_UN)
static void stress_fd_flock(int fd)
{
	if (flock(fd, LOCK_EX) < 0)
		return;
	(void)flock(fd, LOCK_UN);
}
#endif

#if defined(F_NOTIFY) &&	\
    defined(DN_ACCESS)
static void stress_fd_fnctl_f_notify(int fd)
{
	(void)fcntl(fd, F_NOTIFY, DN_ACCESS);
	(void)fcntl(fd, F_NOTIFY, 0);
}
#endif

#if defined(F_SETFL)
static void stress_fd_fcntl_f_setfl(int fd)
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

	old_flag = fcntl(fd, F_GETFL);
	if (old_flag < 0)
		return;
	(void)fcntl(fd, F_SETFL, old_flag | new_flag);
	(void)fcntl(fd, F_SETFL, old_flag & ~new_flag);
	(void)fcntl(fd, F_SETFL, old_flag);
}
#endif

#if defined(F_GETOWN)
static void stress_fd_fcntl_f_getown(int fd)
{
	(void)fcntl(fd, F_GETOWN);
}
#endif

#if defined(F_SETPIPE_SZ)
static void stress_fd_fctnl_f_setpipe_sz(int fd)
{
	(void)fcntl(fd, F_SETPIPE_SZ, 8192);
	(void)fcntl(fd, F_SETPIPE_SZ, 4096);
}
#endif

#if defined(F_SET_RW_HINT)
static void stress_fd_fcntl_f_set_rw_hint(int fd)
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
	(void)fcntl(fd, F_SET_RW_HINT, &hint);
	hint = hints[stress_mwc8modn(SIZEOF_ARRAY(hints))];
	(void)fcntl(fd, F_SET_FILE_RW_HINT, &hint);
}
#endif

#if defined(F_SETLEASE) &&	\
    defined(F_RDLCK) &&		\
    defined(F_WRLCK) &&		\
    defined(F_UNLCK)
static void stress_fd_fcntl_f_setlease(int fd)
{
	if (fcntl(fd, F_SETLEASE, stress_mwc1() ? F_RDLCK : F_WRLCK) < 0)
		return;
	(void)fcntl(fd, F_SETLEASE, F_UNLCK);
}
#endif

#if defined(HAVE_WAITID)
static void stress_fd_waitid(int fd)
{
	siginfo_t info;

	(void)shim_memset(&info, 0, sizeof(info));
	(void)waitid(P_PIDFD, (id_t)fd, &info, WNOHANG);
}
#endif

#if defined(HAVE_SETNS)
static void stress_fd_setns(int fd)
{
	(void)setns(fd, 0);
}
#endif

#if defined(HAVE_LOCKF) &&	\
    defined(F_TLOCK) &&		\
    defined(F_UNLOCK)
static void stress_fd_lockf(int fd)
{
	if (lockf(fd, F_TLOCK, 0) < 0)
		return;
	(void)lockf(fd, F_UNLOCK);
}
#endif

#if (defined(HAVE_SYS_XATTR_H) ||	\
     defined(HAVE_ATTR_XATTR_H)) &&	\
    defined(HAVE_FLISTXATTR)
static void stress_fd_flistxattr(int fd)
{
	char buffer[4096];

	(void)flistxattr(fd, buffer, sizeof(buffer));
}
#endif

#if defined(HAVE_VMSPLICE) &&	\
    defined(SPLICE_F_NONBLOCK)
static void stress_fd_vmslice(int fd)
{
	struct iovec iov[1];
	const size_t sz = 4096;
	void *ptr;

	ptr = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED)
		return;

	iov[0].iov_base = ptr;
	iov[0].iov_len = sz;

	(void)vmsplice(fd, iov, 1, SPLICE_F_NONBLOCK);

	(void)munmap(ptr, sz);
}
#endif

static fd_func_t fd_funcs[] = {
	stress_fd_sockopt_reuseaddr,
	stress_fd_lseek,
	stress_fd_dup,
	stress_fd_dup2,
#if defined(O_CLOEXEC)
	stress_fd_dup3,
#endif
	stress_fd_bind_af_inet,
	stress_fd_bind_af_inet6,
	stress_fd_select_rd,
	stress_fd_select_wr,
#if defined(HAVE_PSELECT)
	stress_fd_pselect_rdwr,
#endif
	stress_fd_poll_rdwr,
#if defined(HAVE_PPOLL)
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
#if defined(POSIX_FADV_RANDOM) ||	\
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
};

/*
 *  Handle and ignore SIGIO/SIGPIPE signals
 */
static void stress_fd_sig_handler(int sig)
{
	(void)sig;
}

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
static int stress_fd_abuse(stress_args_t *args)
{
	size_t i, n;
	int rc = EXIT_SUCCESS;
	pid_t pid;
	int fds[SIZEOF_ARRAY(open_funcs)];

	if (stress_sighandler(args->name, SIGIO, stress_fd_sig_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGPIPE, stress_fd_sig_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	if (stress_temp_dir_mk_args(args) < 0) {
		shim_memset(stress_fd_filename, 0, sizeof(stress_fd_filename));
	} else {
		(void)stress_temp_filename_args(args, stress_fd_filename,
			sizeof(stress_fd_filename), stress_mwc32());
	}

	if (args->instance == 0)
		pr_dbg("%s: %zd fd opening operations, %zd fd exercising operations\n",
			args->name, SIZEOF_ARRAY(open_funcs), SIZEOF_ARRAY(fd_funcs));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0, n = 0; i < SIZEOF_ARRAY(fds); i++) {
		const int fd = open_funcs[i]();

		if (fd < 0)
			continue;
		fds[n++] = fd;
	}

	/*
	 *  Parent and child processes operated on same
	 *  fds for more of a stress mix.
	 */
	pid = fork();
	do {
		size_t j;

		for (i = 0; stress_continue(args) && (i < n); i++) {
			for (j = 0; stress_continue(args) && (j < SIZEOF_ARRAY(fd_funcs)); j++) {
				fd_funcs[j](fds[i]);
				if (pid > -1)
					stress_bogo_inc(args);
			}
		}

		for (i = 0 ; stress_continue(args) && (i < 20); i++) {
			register const size_t func_idx = stress_mwc8modn(SIZEOF_ARRAY(fd_funcs));
			register const size_t fd_idx = stress_mwc8modn(n);

			fd_funcs[func_idx](fd_idx);
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

	if (*stress_fd_filename) {
		shim_unlink(stress_fd_filename);
		stress_temp_dir_rm_args(args);
	}
	for (i = 0; i < n; i++)
		(void)close(fds[i]);

	return rc;
}

const stressor_info_t stress_fd_abuse_info = {
	.stressor = stress_fd_abuse,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
