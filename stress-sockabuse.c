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
	{ NULL,	NULL,			NULL }
};

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
	VOID_RET(int, fstat(fd, &statbuf));
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

		if (gettimeofday(&now, NULL) == 0) {
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
	if (nfd >= 0)
		(void)close(nfd);
#if defined(HAVE_POSIX_FADVISE) &&	\
    defined(POSIX_FADV_RANDOM)
	VOID_RET(int, posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM));
#endif
	VOID_RET(int, shim_sync_file_range(fd, 0, 1, 0));
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
	(void)sched_settings_apply(true);

	do {
		char buf[SOCKET_BUF];
		int fd;
		ssize_t n;
		socklen_t addr_len = 0;
		uint64_t delay = 10000;

retry:
		if (!keep_stressing_flag()) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			AF_INET, socket_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
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
		if (n < 0) {
			if ((errno != EINTR) && (errno != ECONNRESET))
				pr_fail("%s: recv failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
		}

		stress_sockabuse_fd(fd);

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing(args));

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
	double t1 = 0.0, t2 = 0.0, dt;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	t1 = stress_time_now();
	do {
		int i;

		if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			rc = exit_status(errno);
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			continue;
		}
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
			&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			rc = exit_status(errno);
			pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
				pr_fail("%s: bind failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}
		if (listen(fd, 10) < 0) {
			pr_fail("%s: listen failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;

			stress_sockabuse_fd(fd);
			(void)close(fd);
			continue;
		}

		for (i = 0; i < 16; i++) {
			int sfd;

			if (!keep_stressing(args))
				break;

			sfd = accept(fd, (struct sockaddr *)NULL, NULL);
			if (sfd >= 0) {
				struct sockaddr saddr;
				socklen_t len;
				int sndbuf;
				ssize_t n;

				len = sizeof(saddr);
				if (getsockname(fd, &saddr, &len) < 0) {
					pr_fail("%s: getsockname failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(sfd);
					break;
				}
				len = sizeof(sndbuf);
				if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0) {
					pr_fail("%s: getsockopt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(sfd);
					break;
				}
				(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));

				n = send(sfd, buf, sizeof(buf), 0);
				if (n < 0) {
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
		inc_counter(args);
		stress_sockabuse_fd(fd);
		(void)close(fd);
	} while (keep_stressing(args));
	t2 = stress_time_now();

die:
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);
	dt = t2 - t1;
	if (dt > 0.0) {
		stress_misc_stats_set(args->misc_stats, 0,
			"messages sent per sec", (double)msgs / dt);
	}

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
	int socket_port = DEFAULT_SOCKABUSE_PORT + (int)args->instance;
	int rc = EXIT_SUCCESS;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sockabuse_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

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
		stress_sockabuse_client(args, ppid,
			socket_port);
		_exit(rc);
	} else {
		rc = stress_sockabuse_server(args, pid, ppid,
			socket_port);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_sockabuse_info = {
	.stressor = stress_sockabuse,
	.class = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
