/*
 * Copyright (C)      2024 Colin Ian King.
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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-madvise.h"
#include "core-net.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#define DEFAULT_SOCKET_PORT	(16000)

#if !defined(IPPROTO_TCP)
#define IPPROTO_TCP		(0)
#endif

static const stress_help_t help[] = {
	{ NULL, "sigurg N",		"start N workers exercising SIGURG on MSB_OOB socket sends" },
	{ NULL,	"sigurg-ops N",		"stop after N SIGURG signals" },
	{ NULL,	NULL,			NULL }
};

#if defined(SIOCATMARK) &&	\
    defined(AF_INET) &&		\
    defined(SOCK_STREAM) &&	\
    defined(IPPROTO_TCP)

static stress_args_t *s_args;
static int sockfd = -1;

static void stress_sigurg_handler(int signum)
{
	static char buf[1];

	(void)signum;

	if (LIKELY(stress_continue(s_args) &&
		  (recv(sockfd, buf, sizeof(buf), MSG_OOB) > 0)))
		stress_bogo_inc(s_args);
}

/*
 *  stress_sigurg_client()
 *	client reader
 */
static int OPTIMIZE3 stress_sigurg_client(
	stress_args_t *args,
	const pid_t mypid,
	const int sock_port)
{
	struct sockaddr *addr;
	int rc = EXIT_FAILURE;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);
	(void)signal(SIGPIPE, SIG_IGN);

	do {
		int retries = 0;
		int ret;
		socklen_t addr_len = 0;

retry:
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;

		sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (UNLIKELY(sockfd < 0)) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		if (UNLIKELY(stress_set_sockaddr_if(args->name, args->instance, mypid,
				AF_INET, sock_port, NULL,
				&addr, &addr_len, NET_ADDR_ANY) < 0)) {
			(void)close(sockfd);
			sockfd = -1;
			return EXIT_FAILURE;
		}
		if (UNLIKELY(connect(sockfd, addr, addr_len) < 0)) {
			const int errno_tmp = errno;

			(void)close(sockfd);
			sockfd = -1;
			(void)shim_usleep(10000);
			retries++;
			if (UNLIKELY(retries > 100)) {
				/* Give up.. */
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno_tmp, strerror(errno_tmp));
				return EXIT_FAILURE;
			}
			goto retry;
		}
#if defined(O_ASYNC)
		{
			int flags;

			flags = fcntl(sockfd, F_GETFD);
			if (LIKELY(flags >= 0)) {
				flags |= O_ASYNC;
				VOID_RET(int, fcntl(sockfd, F_SETFD, flags));
			}
		}
#endif
		ret = fcntl(sockfd, F_SETOWN, getpid());
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: fcntl F_SETOWN, failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(sockfd);
			return EXIT_FAILURE;
		}

		do {
			char buf[1];
			ssize_t n;
			int atmark = 0;	/* silence valgrind warnings */

			ret = ioctl(sockfd, SIOCATMARK, &atmark);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: ioctl SIOCATMARK failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sockfd);
				return EXIT_FAILURE;
			} else {
				if (atmark)
					continue;
			}
			n = recv(sockfd, buf, sizeof(buf), 0);
			(void)n;
		} while (stress_continue(args));

		(void)shutdown(sockfd, SHUT_RDWR);
		(void)close(sockfd);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;

	return rc;
}

static inline bool stress_send_error(const int err)
{
	return ((err != EINTR) &&
		(err != EPIPE) &&
		(err != ECONNRESET));
}

/*
 *  stress_sigurg_server()
 *	server writer
 */
static int OPTIMIZE3 stress_sigurg_server(
	stress_args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int sock_port)
{
	int fd;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	int rc = EXIT_SUCCESS;

	(void)signal(SIGPIPE, SIG_IGN);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}

	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
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

	if (stress_set_sockaddr_if(args->name, args->instance, ppid,
			AF_INET, sock_port, NULL,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
		goto die_close;
	}

	if (bind(fd, addr, addr_len) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: bind failed on port %d, errno=%d (%s)\n",
			args->name, sock_port, errno, strerror(errno));
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

		if (UNLIKELY(!stress_continue(args)))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			do {
				static char buf[1] = { 'x' };

				if (UNLIKELY(send(sfd, buf, sizeof(buf), MSG_OOB) < 0)) {
					if (errno == ENOBUFS)
						continue;
					if (stress_send_error(errno)) {
						pr_fail("%s: send failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
					}
					break;
				}
			} while (stress_continue(args));
		}
		(void)close(sfd);
	} while (stress_continue(args));

die_close:
	(void)close(fd);
die:
	if (pid)
		(void)stress_kill_pid_wait(pid, NULL);
	return rc;
}


/*
 *  stress_sigurg
 *	stress by heavy socket I/O
 */
static int stress_sigurg(stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	int sock_port = DEFAULT_SOCKET_PORT;
	int rc = EXIT_SUCCESS, reserved_port, parent_cpu;

	s_args = args;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	sock_port += args->instance;
	if (sock_port > MAX_PORT)
		sock_port -= (MAX_PORT- MIN_PORT + 1);
	reserved_port = stress_net_reserve_ports(sock_port, sock_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, sock_port);
		return EXIT_NO_RESOURCE;
	}
	sock_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, sock_port);

	if (stress_sighandler(args->name, SIGURG, stress_sigurg_handler, NULL) < 0)
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

		rc = stress_sigurg_client(args, mypid, sock_port);
		_exit(rc);
	} else {
		rc = stress_sigurg_server(args, pid, mypid, sock_port);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(sock_port, sock_port);

	return rc;
}

const stressor_info_t stress_sigurg_info = {
	.stressor = stress_sigurg,
	.classifier = CLASS_SIGNAL | CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help
};

#else

const stressor_info_t stress_sigurg_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without SIOCATMARK, AF_INET, SOCK_STREAM or IPPROTO_TCP socket support",
};

#endif
