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

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#else
UNEXPECTED
#endif

#define DEFAULT_SOCKET_MANY_PORT (11000)
#define SOCKET_MANY_BUF		(8)
#define SOCKET_MANY_FDS		(100000)

typedef struct {
	int max_fd;
	int fds[SOCKET_MANY_FDS];
} stress_sock_fds_t;

static const stress_help_t help[] = {
	{ NULL, "sockmany N",		"start N workers exercising many socket connections" },
	{ NULL,	"sockmany-ops N",	"stop after N sockmany bogo operations" },
	{ NULL,	"sockmany-if I",	"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	NULL,			NULL }
};

static int stress_set_sockmany_if(const char *name)
{
	return stress_set_setting("sockmany-if", TYPE_ID_STR, name);
}

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
	const int socket_port,
	const pid_t mypid,
	stress_sock_fds_t *sock_fds,
	const char *sockmany_if)
{
	struct sockaddr *addr;
	static int fds[SOCKET_MANY_FDS];

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	sock_fds->max_fd = 0;

	do {
		int i;

		for (i = 0; i < SOCKET_MANY_FDS; i++) {
			char ALIGN64 buf[SOCKET_MANY_BUF];
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
				pr_fail("%s: socket failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				stress_sockmany_cleanup(fds, i);
				return EXIT_FAILURE;
			}

			if (stress_set_sockaddr_if(args->name, args->instance, mypid,
					AF_INET, socket_port, sockmany_if,
					&addr, &addr_len, NET_ADDR_ANY) < 0) {
				return EXIT_FAILURE;
			}
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
					pr_fail("%s: connect failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					return EXIT_FAILURE;
				}
				goto retry;
			}

			n = recv(fds[i], buf, sizeof(buf), 0);
			if (n < 0) {
				if ((errno != EINTR) && (errno != ECONNRESET))
					pr_fail("%s: recv failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				break;
			}
			if (i > sock_fds->max_fd)
				sock_fds->max_fd = i;
		}
		stress_sockmany_cleanup(fds, i);
	} while (keep_stressing(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_sockmany_server()
 *	server writer
 */
static int stress_sockmany_server(
	const stress_args_t *args,
	const int socket_port,
	const pid_t pid,
	const pid_t mypid,
	const char *sockmany_if)
{
	char ALIGN64 buf[SOCKET_MANY_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
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

	if (stress_set_sockaddr_if(args->name, args->instance, mypid,
			AF_INET, socket_port, sockmany_if,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (bind(fd, addr, addr_len) < 0) {
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

		if (!keep_stressing(args))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			ssize_t sret;
			struct sockaddr saddr;
			socklen_t len;
			int sndbuf;
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
#else
			UNEXPECTED
#endif
			(void)memset(buf, 'A' + (msgs % 26), sizeof(buf));
			sret = send(sfd, buf, sizeof(buf), 0);
			if (sret < 0) {
				if ((errno != EINTR) && (errno != EPIPE))
					pr_fail("%s: send failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			} else {
				msgs++;
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing(args));

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
	int socket_port = DEFAULT_SOCKET_MANY_PORT;
	int rc = EXIT_SUCCESS, reserved_port;
	char *sockmany_if = NULL;

	(void)stress_get_setting("sockmany-if", &sockmany_if);

	if (sockmany_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(sockmany_if, AF_INET, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, sockmany_if, stress_net_domain(AF_INET));
			sockmany_if = NULL;
		}
	}

	socket_port += args->instance;
	reserved_port = stress_net_reserve_ports(socket_port, socket_port);
	if (reserved_port < 0) {
		pr_inf("%s: cannot reserve port %d, skipping stressor\n",
			args->name, socket_port);
		return EXIT_NO_RESOURCE;
	}
	socket_port = reserved_port;
	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, socket_port);

	sock_fds = (stress_sock_fds_t *)mmap(NULL, sizeof(*sock_fds), PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (sock_fds == MAP_FAILED) {
		pr_inf("%s: could not allocate share memory, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGPIPE, stress_sockmany_sigpipe_handler, NULL) < 0)
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
		rc = EXIT_FAILURE;
	} else if (pid == 0) {
		rc = stress_sockmany_client(args, socket_port, ppid, sock_fds, sockmany_if);

		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);

		_exit(rc);
	} else {
		rc = stress_sockmany_server(args, socket_port, pid, ppid, sockmany_if);
	}
	pr_dbg("%s: %d sockets opened at one time\n", args->name, sock_fds->max_fd);

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(socket_port, socket_port);

	(void)munmap((void *)sock_fds, args->page_size);
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sockmany_if,	stress_set_sockmany_if },
	{ 0,			NULL },
};

stressor_info_t stress_sockmany_info = {
	.stressor = stress_sockmany,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
