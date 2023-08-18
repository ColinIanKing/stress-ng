// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-net.h"

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#define MIN_SOCKET_FD_PORT	(1024)
#define MAX_SOCKET_FD_PORT	(65535)
#define DEFAULT_SOCKET_FD_PORT	(8000)

static const stress_help_t help[] = {
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
		MIN_SOCKET_FD_PORT, MAX_SOCKET_FD_PORT, &socket_fd_port);
	return stress_set_setting("sockfd-port", TYPE_ID_INT, &socket_fd_port);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sockfd_port,	stress_set_socket_fd_port },
	{ 0,			NULL }
};

#if defined(__linux__)

#define MSG_ID			'M'

/*
 *  stress_socket_fd_send()
 *	send a fd (fd_send) over a socket fd
 */
static inline ssize_t stress_socket_fd_sendmsg(const int fd, const int fd_send)
{
	struct iovec iov;
	struct msghdr msg ALIGN64;
	struct cmsghdr *cmsg;
	int *ptr;

	char ctrl[CMSG_SPACE(sizeof(int))];
	static char msg_data[1] = { MSG_ID };

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)shim_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)shim_memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));

	ptr = (int *)(uintptr_t)CMSG_DATA(cmsg);
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
	struct msghdr ALIGN64 msg;
	struct cmsghdr *cmsg;
	char msg_data[1] = { 0 };
	char ctrl[CMSG_SPACE(sizeof(int))];

	iov.iov_base = msg_data;
	iov.iov_len = 1;

	(void)shim_memset(&msg, 0, sizeof(msg));
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;

	(void)shim_memset(ctrl, 0, sizeof(ctrl));
	msg.msg_control = ctrl;
	msg.msg_controllen = sizeof(ctrl);

	if (UNLIKELY(recvmsg(fd, &msg, 0) <= 0))
		return -1;
	if (UNLIKELY(msg_data[0] != MSG_ID))
		return -1;
	if (UNLIKELY((msg.msg_flags & MSG_CTRUNC) == MSG_CTRUNC))
		return -1;

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg &&
	    (cmsg->cmsg_level == SOL_SOCKET) &&
	    (cmsg->cmsg_type == SCM_RIGHTS) &&
	    ((size_t)cmsg->cmsg_len >= (size_t)CMSG_LEN(sizeof(int)))) {
		int *const ptr = (int *)(uintptr_t)CMSG_DATA(cmsg);
		return *ptr;
	}

	return -1;
}

/*
 *  stress_socket_client()
 *	client reader
 */
static int OPTIMIZE3 stress_socket_client(
	const stress_args_t *args,
	const pid_t mypid,
	const ssize_t max_fd,
	const int socket_fd_port,
	int *fds,
	const size_t fds_size)
{
	struct sockaddr *addr = NULL;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		int fd, retries = 0;
		ssize_t n;
		socklen_t addr_len = 0;
		int so_reuseaddr = 1;

		(void)shim_memset(fds, 0, fds_size);
retry:
		if (!stress_continue_flag())
			return EXIT_NO_RESOURCE;

		if (UNLIKELY((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (UNLIKELY(setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
				&so_reuseaddr, sizeof(so_reuseaddr)) < 0)) {
			(void)close(fd);
			pr_fail("%s: setsockopt SO_REUSEADDR failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if (UNLIKELY(stress_set_sockaddr(args->name, args->instance, mypid,
				AF_UNIX, socket_fd_port,
				&addr, &addr_len, NET_ADDR_ANY) < 0)) {
			return EXIT_FAILURE;
		}
		if (UNLIKELY(connect(fd, addr, addr_len) < 0)) {
			(void)close(fd);
			if (retries++ > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_NO_RESOURCE;
			}
			(void)shim_usleep(10000);
			goto retry;
		}

		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;

		for (n = 0; stress_continue(args) && (n < max_fd); n++) {
			int rc, nbytes;

			fds[n] = stress_socket_fd_recv(fd);
			if (fds[n] < 0)
				continue;

			/* Attempt to read a byte from the fd */
			rc = ioctl(fds[n], FIONREAD, &nbytes);
			if ((rc == 0) && (nbytes >= 1)) {
				char data;

				VOID_RET(ssize_t, read(fds[n], &data, sizeof(data)));
			}
		}

		stress_close_fds(fds, n);
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (stress_continue(args));

#if defined(HAVE_SOCKADDR_UN)
	if (addr) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	return EXIT_SUCCESS;

}

/*
 *  stress_socket_server()
 *	server writer
 */
static int OPTIMIZE3 stress_socket_server(
	const stress_args_t *args,
	const pid_t ppid,
	const ssize_t max_fd,
	const int socket_fd_port)
{
	int fd;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	const int bad_fd = stress_get_bad_fd();

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_UNIX, SOCK_STREAM, 0)) < 0) {
		rc = stress_exit_status(errno);
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

	if (stress_set_sockaddr(args->name, args->instance, ppid,
			AF_UNIX, socket_fd_port,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (bind(fd, addr, addr_len) < 0) {
		if (errno == EADDRINUSE) {
			rc = EXIT_NO_RESOURCE;
			pr_inf_skip("%s: cannot bind, skipping stressor, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto die_close;
		}
		rc = stress_exit_status(errno);
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

	do {
		int sfd;

		if (!stress_continue(args))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			ssize_t i;

			for (i = 0; stress_continue(args) && (i < max_fd); i++) {
				int new_fd;

				new_fd = open("/dev/zero", O_RDWR);
				if (new_fd >= 0) {
					ssize_t ret;

					ret = stress_socket_fd_sendmsg(sfd, new_fd);
					if ((ret < 0) &&
					     ((errno != EAGAIN) &&
					      (errno != EINTR) &&
					      (errno != EWOULDBLOCK) &&
					      (errno != ECONNRESET) &&
					      (errno != ENOMEM) &&
#if defined(ETOOMANYREFS)
					      (errno != ETOOMANYREFS) &&
#endif
					      (errno != EPIPE))) {
						pr_fail("%s: sendmsg failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						(void)close(new_fd);
						break;
					}
					(void)close(new_fd);
					VOID_RET(ssize_t, stress_socket_fd_sendmsg(sfd, bad_fd));
					msgs++;
					stress_bogo_inc(args);
				}
			}
			(void)close(sfd);
		}
	} while (stress_continue(args));

die_close:
	(void)close(fd);
die:
#if defined(HAVE_SOCKADDR_UN)
	if (addr) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	return rc;
}

/*
 *  stress_sockfd
 *	stress socket fd passing
 */
static int stress_sockfd(const stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	ssize_t max_fd = (ssize_t)stress_get_file_limit();
	int socket_fd_port = DEFAULT_SOCKET_FD_PORT;
	int ret = EXIT_SUCCESS, reserved_port;
	int *fds;
	size_t fds_size;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("sockfd-port", &socket_fd_port);

	socket_fd_port += args->instance;
	reserved_port = stress_net_reserve_ports(socket_fd_port, socket_fd_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, socket_fd_port);
		return EXIT_NO_RESOURCE;
	}
	socket_fd_port = reserved_port;

	pr_dbg("%s: process [%" PRIdMAX "] using socket port %d and %zd file descriptors\n",
		args->name, (intmax_t)args->pid, socket_fd_port, max_fd);

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

	fds_size = sizeof(*fds) * (size_t)max_fd;
	fds = malloc(fds_size);
	if (!fds) {
		pr_inf_skip("%s: cannot allocate %zd file descriptors, skipping stressor\n",
			args->name, max_fd);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args)) {
			ret = EXIT_SUCCESS;
			goto finish;
		}
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		free(fds);
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_set_oom_adjustment(args, false);
		ret = stress_socket_client(args, mypid, max_fd, socket_fd_port, fds, fds_size);
		_exit(ret);
	} else {
		int status;

		ret = stress_socket_server(args, mypid, max_fd, socket_fd_port);
		(void)shim_kill(pid, SIGALRM);
		(void)shim_waitpid(pid, &status, 0);
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(fds);

	return ret;
}

stressor_info_t stress_sockfd_info = {
	.stressor = stress_sockfd,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sockfd_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
