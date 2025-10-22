/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
    defined(POSIX_FADV_RANDOM)
	VOID_RET(int, posix_fadvise(fd, 0, 0, POSIX_FADV_RANDOM));
#endif
	VOID_RET(int, shim_sync_file_range(fd, 0, 1, 0));
#if defined(HAVE_FUTIMENS)
	(void)shim_memset(&timespec, 0, sizeof(timespec));
#endif
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
	(void)sched_settings_apply(true);

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

		if (UNLIKELY(stress_set_sockaddr(args->name, args->instance, mypid,
				AF_INET, sockabuse_port,
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

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
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

		if (UNLIKELY(stress_set_sockaddr(args->name, args->instance, mypid,
				AF_INET, sockabuse_port,
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
		stress_sockabuse_fd(fd);
		(void)close(fd);
	} while (stress_continue(args));
	t2 = stress_time_now();

die:
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);
	dt = t2 - t1;
	if (dt > 0.0)
		stress_metrics_set(args, 0, "messages sent per sec",
			(double)msgs / dt, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

static void stress_sockabuse_sigpipe_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
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

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("sockabuse-port", &sockabuse_port);

	sockabuse_port += args->instance;
	if (sockabuse_port > MAX_PORT)
		sockabuse_port -= (MAX_PORT - MIN_PORT + 1);
	reserved_port = stress_net_reserve_ports(sockabuse_port, sockabuse_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, sockabuse_port);
		return EXIT_NO_RESOURCE;
	}
	sockabuse_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, sockabuse_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sockabuse_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
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
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);

		rc = stress_sockabuse_client(args, mypid, sockabuse_port);
		_exit(rc);
	} else {
		rc = stress_sockabuse_server(args, mypid, sockabuse_port);
		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
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
