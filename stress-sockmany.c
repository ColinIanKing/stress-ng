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

#define SOCKET_MANY_BUF		(8)
#define SOCKET_MANY_FDS		(100000)

typedef struct {
	int max_fd;
	int fds[SOCKET_MANY_FDS];
} stress_sock_fds_t;

static const stress_help_t help[] = {
	{ NULL, "sockmany N",		"start N workers exercising many socket connections" },
	{ NULL,	"sockmany-ops N",	"stop after N sockmany bogo operations" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_sockmany_cleanup()
 *	close sockets
 */
static void stress_sockmany_cleanup(int fds[], const int n)
{
	int i;

	for (i = 0; i < n; i++) {
		if (fds[i] >= 0) {
			(void)shutdown(fds[i], SHUT_RDWR);
			(void)close(fds[i]);
		}
		fds[i] = -1;
	}
}

/*
 *  stress_sockmany_client()
 *	client reader
 */
static int stress_sockmany_client(
	const stress_args_t *args,
	const pid_t ppid,
	stress_sock_fds_t *sock_fds)
{
	struct sockaddr *addr;
	static int fds[SOCKET_MANY_FDS];
	int rc = EXIT_FAILURE;
	const int socket_port = DEFAULT_SOCKET_MANY_PORT + args->instance;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	sock_fds->max_fd = 0;

	do {
		int i;

		for (i = 0; i < SOCKET_MANY_FDS; i++) {
			char buf[SOCKET_MANY_BUF];
			ssize_t n;
			int retries = 0;
			socklen_t addr_len = 0;
retry:
			if (!keep_stressing_flag()) {
				stress_sockmany_cleanup(fds, i);
				break;
			}
			if ((fds[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				/* Out of resources? */
				if ((errno == EMFILE) ||
				    (errno == ENFILE) ||
				    (errno == ENOBUFS) ||
				    (errno == ENOMEM))
					break;

				/* Something unexpected went wrong */
				pr_fail_dbg("socket");
				stress_sockmany_cleanup(fds, i);
				goto finish;
			}

			stress_set_sockaddr(args->name, args->instance, ppid,
				AF_INET, socket_port,
				&addr, &addr_len, NET_ADDR_ANY);
			if (connect(fds[i], addr, addr_len) < 0) {
				int save_errno = errno;

				(void)close(fds[i]);

				/* Run out of resources? */
				if (errno == EADDRNOTAVAIL)
					break;

				(void)shim_usleep(10000);
				retries++;
				if (retries > 100) {
					/* Give up.. */
					stress_sockmany_cleanup(fds, i);
					errno = save_errno;
					pr_fail_dbg("connect");
					goto finish;
				}
				goto retry;
			}

			n = recv(fds[i], buf, sizeof(buf), 0);
			if (n < 0) {
				if ((errno != EINTR) && (errno != ECONNRESET))
					pr_fail_dbg("recv");
				break;
			}
			if (i > sock_fds->max_fd)
				sock_fds->max_fd = i;
		}
		stress_sockmany_cleanup(fds, i);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
finish:
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
	return rc;
}

/*
 *  stress_sockmany_server()
 *	server writer
 */
static int stress_sockmany_server(
	const stress_args_t *args,
	const pid_t pid,
	const pid_t ppid)
{
	char buf[SOCKET_MANY_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;
	const int socket_port = DEFAULT_SOCKET_MANY_PORT + args->instance;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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
		AF_INET, socket_port,
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
			ssize_t sret;
			struct sockaddr saddr;
			socklen_t len;
			int sndbuf;
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
#if defined(SOL_TCP) && defined(TCP_QUICKACK)
			{
				int ret, one = 1;
				/*
				 * We try do to a TCP_QUICKACK, failing is OK as
				 * it's just a faster optimization option
				 */
				ret = setsockopt(fd, SOL_TCP, TCP_QUICKACK, &one, sizeof(one));
				(void)ret;
			}
#endif
			(void)memset(buf, 'A' + (msgs % 26), sizeof(buf));
			sret = send(sfd, buf, sizeof(buf), 0);
			if (sret < 0) {
				if ((errno != EINTR) && (errno != EPIPE))
					pr_fail_dbg("send");
				(void)close(sfd);
				break;
			} else {
				msgs++;
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	return rc;
}

static void stress_sockmany_sigpipe_handler(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
}

/*
 *  stress_sockmany
 *	stress many sockets
 */
static int stress_sockmany(const stress_args_t *args)
{
	pid_t pid, ppid = getppid();
	stress_sock_fds_t *sock_fds;
	int rc = EXIT_SUCCESS;

	sock_fds = (stress_sock_fds_t *)mmap(NULL, sizeof(stress_sock_fds_t), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sock_fds == MAP_FAILED) {
		pr_inf("%s: could not allocate share memory, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGPIPE, stress_sockmany_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		rc = EXIT_FAILURE;
	} else if (pid == 0) {
		rc = stress_sockmany_client(args, ppid, sock_fds);
		_exit(rc);
	} else {
		rc = stress_sockmany_server(args, pid, ppid);
	}
	pr_dbg("%s: %d sockets opened at one time\n", args->name, sock_fds->max_fd);
	(void)munmap((void *)sock_fds, args->page_size);
	return rc;
}

stressor_info_t stress_sockmany_info = {
	.stressor = stress_sockmany,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
