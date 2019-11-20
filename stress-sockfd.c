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

static const help_t help[] = {
	{ NULL,	"sockfd N",	 "start N workers sending file descriptors over sockets" },
	{ NULL,	"sockfd-ops N",	 "stop after N sockfd bogo operations" },
	{ NULL,	"sockfd-port P", "use socket fd ports P to P + number of workers - 1" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  stress_set_socket_fd_port()
 *	set port to use
 */
static int stress_set_socket_fd_port(const char *opt)
{
	int socket_fd_port;

	stress_set_net_port("sockfd-port", opt,
		MIN_SOCKET_FD_PORT, MAX_SOCKET_FD_PORT - STRESS_PROCS_MAX,
		&socket_fd_port);
	return set_setting("sockfd-port", TYPE_ID_INT, &socket_fd_port);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sockfd_port,	stress_set_socket_fd_port },
	{ 0,			NULL }
};

#if defined(__linux__)

#define MSG_ID			'M'

/*
 *  stress_socket_fd_send()
 *	send a fd (fd_send) over a socket fd
 */
static inline int stress_socket_fd_sendmsg(const int fd, const int fd_send)
{
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	int *ptr;

	char ctrl[CMSG_SPACE(sizeof(int))];
	static char msg_data[1] = { MSG_ID };

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	ptr = (int *)CMSG_DATA(cmsg);
	*ptr = fd_send;
	return sendmsg(fd, &msg, 0);
}

/*
 *  stress_socket_fd_recv()
 *	recv an fd over a socket, return fd or -1 if fail
 */
static inline int stress_socket_fd_recv(const int fd)
{
	struct iovec iov;
	struct msghdr msg;
	struct cmsghdr *cmsg;
	char msg_data[1];
	char ctrl[CMSG_SPACE(sizeof(int))];

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)memset(&msg, 0, sizeof(struct msghdr));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	if (recvmsg(fd, &msg, 0) <= 0)
		return -errno;
	if (msg_data[0] != MSG_ID)
		return -1;
	if ((msg.msg_flags & MSG_CTRUNC) == MSG_CTRUNC)
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg &&
	    (cmsg->cmsg_level == SOL_SOCKET) &&
	    (cmsg->cmsg_type == SCM_RIGHTS) &&
	    ((size_t)cmsg->cmsg_len >= (size_t)CMSG_LEN(sizeof(int)))) {
		int *const ptr = (int *)CMSG_DATA(cmsg);
		return *ptr;
	}

	return -1;
}

/*
 *  stress_socket_client()
 *	client reader
 */
static void stress_socket_client(
	const args_t *args,
	const pid_t ppid,
	const ssize_t max_fd,
	const int socket_fd_port)
{
	struct sockaddr *addr = NULL;
	int ret = EXIT_FAILURE;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		int fd, retries = 0, fds[max_fd];
		ssize_t i, n;
		socklen_t addr_len = 0;

		(void)memset(fds, 0, sizeof(fds));
retry:
		if (!g_keep_stressing_flag) {
			ret = EXIT_SUCCESS;
			goto finish;
		}

		if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
			pr_fail_dbg("socket");
			goto finish;
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			AF_UNIX, socket_fd_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail_dbg("connect");
				goto finish;
			}
			goto retry;
		}

		if (!g_keep_stressing_flag) {
			ret = EXIT_SUCCESS;
			goto finish;
		}

		for (n = 0; keep_stressing() && (n < max_fd); n++)
			fds[n] = stress_socket_fd_recv(fd);

		for (i = 0; i < n; i++) {
			if (fds[i] >= 0)
				(void)close(fds[i]);
		}

		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

	if (addr) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}

	ret = EXIT_SUCCESS;

finish:
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
	_exit(ret);
}

/*
 *  stress_socket_server()
 *	server writer
 */
static int stress_socket_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const ssize_t max_fd,
	const int socket_fd_port)
{
	int fd, status;
	int so_reuseaddr = 1;
	struct sockaddr_un *addr_un;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
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
		AF_UNIX, socket_fd_port,
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

	do {
		int sfd;

		if (!keep_stressing())
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			ssize_t i;

			for (i = 0; keep_stressing() && (i < max_fd); i++) {
				int newfd;

				newfd = open("/dev/null", O_RDWR);
				if (newfd >= 0) {
					int ret;

					ret = stress_socket_fd_sendmsg(sfd, newfd);
					if ((ret < 0) &&
					     ((errno != EAGAIN) && (errno != EINTR) &&
					      (errno != EWOULDBLOCK) && (errno != ECONNRESET) &&
					      (errno != ENOMEM) && (errno != EPIPE))) {
						pr_fail_dbg("sendmsg");
						(void)close(newfd);
						break;
					}
					(void)close(newfd);
					msgs++;
				}
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
	if (addr) {
		addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}

	if (pid) {
		(void)kill(pid, SIGALRM);
		(void)shim_waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	return rc;
}

/*
 *  stress_sockfd
 *	stress socket fd passing
 */
static int stress_sockfd(const args_t *args)
{
	pid_t pid, ppid = getppid();
	ssize_t max_fd = stress_get_file_limit();
	int socket_fd_port = DEFAULT_SOCKET_FD_PORT;
	int ret = EXIT_SUCCESS;

	(void)get_setting("sockfd-port", &socket_fd_port);

	/*
	 * When run as root, we really don't want to use up all
	 * the file descriptors. Limit ourselves to a head room
	 * so that we don't ever run out of memory
	 */
	if (geteuid() == 0) {
		max_fd -= 64;
		max_fd /= args->num_instances ? args->num_instances : 1;
		if (max_fd < 0)
			max_fd = 1;
	}

	pr_dbg("%s: process [%d] using socket port %d and %zd file descriptors\n",
		args->name, args->pid, socket_fd_port + args->instance, max_fd);

again:
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN) {
			if (g_keep_stressing_flag)
				goto again;
			return EXIT_NO_RESOURCE;
		}
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		set_oom_adjustment(args->name, false);
		stress_socket_client(args, ppid, max_fd, socket_fd_port);
		_exit(EXIT_SUCCESS);
	} else {
		ret = stress_socket_server(args, pid, ppid, max_fd, socket_fd_port);
	}
	return ret;
}

stressor_info_t stress_sockfd_info = {
	.stressor = stress_sockfd,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sockfd_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
