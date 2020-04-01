/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define MSGVEC_SIZE		(4)

static const stress_help_t help[] = {
	{ "S N", "sock N",		"start N workers exercising socket I/O" },
	{ NULL,	"sock-ops N",		"stop after N socket bogo operations" },
	{ NULL,	NULL,			NULL }
};

#define VOID_RET(x)	\
do {			\
	int ret = x;	\
			\
	(void)ret;	\
} while(0)		\

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

	(void)memset(&addr, 0, sizeof(addr));
	VOID_RET(connect(fd, &addr, sizeof(addr)));
	VOID_RET(shim_fdatasync(fd));
	VOID_RET(shim_fsync(fd));
	VOID_RET(shim_fallocate(fd, 0, 4096, 0));
	VOID_RET(fchdir(fd));
	VOID_RET(fchmod(fd, 0660));
	VOID_RET(fchown(fd, uid, gid));
#if defined(F_GETFD)
	VOID_RET(fcntl(fd, F_GETFD));
#endif
#if defined(HAVE_FLOCK) &&      \
    defined(LOCK_UN)
	VOID_RET(flock(fd, LOCK_UN));
#endif
#if (defined(HAVE_SYS_XATTR_H) ||       \
     defined(HAVE_ATTR_XATTR_H)) &&     \
    defined(HAVE_SETXATTR) &&		\
    defined(XATTR_CREATE)
	VOID_RET(shim_fsetxattr(fd, "test", "value", 5, XATTR_CREATE));
#endif
	VOID_RET(fstat(fd, &statbuf));
	VOID_RET(ftruncate(fd, 0));
#if (defined(HAVE_SYS_XATTR_H) ||       \
     defined(HAVE_ATTR_XATTR_H)) &&     \
    defined(HAVE_FLISTXATTR)
	{
		char list[4096];

		VOID_RET(shim_flistxattr(fd, list, sizeof(list)));
	}
#endif
#if defined(HAVE_FUTIMENS)
	VOID_RET(futimens(fd, timespec));
#endif
	addrlen = sizeof(addr);
	VOID_RET(getpeername(fd, &addr, &addrlen));
#if defined(FIONREAD)
	{
		int n;

		VOID_RET(ioctl(fd, FIONREAD, &n));
	}
#endif
#if defined(SEEK_SET)
	VOID_RET(lseek(fd, 0, SEEK_SET));
#endif
	VOID_RET(shim_pidfd_send_signal(fd, SIGUSR1, NULL, 0));
	ptr = mmap(NULL, 4096, PROT_READ, MAP_SHARED, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, 4096);
	ptr = mmap(NULL, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
	if (ptr != MAP_FAILED)
		munmap(ptr, 4096);
	nfd = dup(fd);
	VOID_RET(shim_copy_file_range(fd, 0, nfd, 0, 16, 0));
	if (nfd >= 0)
		(void)close(nfd);
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_RANDOM)
	VOID_RET(posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM));
#endif
	VOID_RET(shim_sync_file_range(fd, 0, 1, 0));
#if defined(HAVE_FUTIMENS)
	(void)memset(&timespec, 0, sizeof(timespec));
#endif
}

/*
 *  stress_sockabuse_client()
 *	client reader
 */
static void stress_sockabuse_client(
	const stress_args_t *args,
	const pid_t ppid,
	const int socket_port)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[SOCKET_BUF];
		int fd, retries = 0;
		ssize_t n;
		socklen_t addr_len = 0;
retry:
		if (!keep_stressing_flag()) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			pr_fail_err("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			AF_INET, socket_port,
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

		n = recv(fd, buf, sizeof(buf), 0);
		if (n < 0) {
			if ((errno != EINTR) && (errno != ECONNRESET))
				pr_fail_dbg("recv");
		}

		stress_sockabuse_fd(fd);

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_sockabuse_server()
 *	server writer
 */
static int stress_sockabuse_server(
	const stress_args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int socket_port)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	do {
		int i;

		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			rc = exit_status(errno);
			pr_fail_err("socket");
			continue;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			rc = exit_status(errno);
			pr_fail_err("setsockopt");

			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			AF_INET, socket_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (bind(fd, addr, addr_len) < 0) {
			if (errno != EADDRINUSE) {
				rc = exit_status(errno);
				pr_fail_err("bind");
			}
			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}
		if (listen(fd, 10) < 0) {
			pr_fail_dbg("listen");
			rc = EXIT_FAILURE;

			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}

		for (i = 0; i < 16; i++) {
			int sfd;

			if (!keep_stressing())
				break;

			sfd = accept(fd, (struct sockaddr *)NULL, NULL);
			if (sfd >= 0) {
				struct sockaddr saddr;
				socklen_t len;
				int sndbuf;
				ssize_t n;

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
				(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));

				n = send(sfd, buf, sizeof(buf), 0);
				if (n < 0) {
					if ((errno != EINTR) && (errno != EPIPE))
						pr_fail_dbg("send");
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
		inc_counter(args);
		stress_sockabuse_fd(fd);
		(void)close(fd);
	} while (keep_stressing());

die:
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	return rc;
}

static void stress_sockabuse_sigpipe_handler(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
}

/*
 *  stress_sockabuse
 *	stress by heavy socket I/O
 */
static int stress_sockabuse(const stress_args_t *args)
{
	pid_t pid, ppid = getppid();
	int socket_port = DEFAULT_SOCKABUSE_PORT + args->instance;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port + args->instance);

	if (stress_sighandler(args->name, SIGPIPE, stress_sockabuse_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sockabuse_client(args, ppid,
			socket_port);
		_exit(EXIT_SUCCESS);
	} else {
		return stress_sockabuse_server(args, pid, ppid,
			socket_port);
	}
}

stressor_info_t stress_sockabuse_info = {
	.stressor = stress_sockabuse,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
