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
	{ NULL,	"epoll N",	  "start N workers doing epoll handled socket activity" },
	{ NULL,	"epoll-ops N",	  "stop after N epoll bogo operations" },
	{ NULL,	"epoll-port P",	  "use socket ports P upwards" },
	{ NULL,	"epoll-domain D", "specify socket domain, default is unix" },
	{ NULL,	NULL,		  NULL }
};

#define MAX_EPOLL_EVENTS 	(1024)
#define MAX_SERVERS		(4)

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    NEED_GLIBC(2,3,2)

typedef void (epoll_func_t)(
	const args_t *args,
	const int child,
	const pid_t ppid,
	const int epoll_port,
	const int epoll_domain);

static timer_t epoll_timerid;

#endif

static int max_servers = 1;

/*
 *  stress_set_epoll_port()
 *	set the default port base
 */
static int stress_set_epoll_port(const char *opt)
{
	int epoll_port;

	stress_set_net_port("epoll-port", opt,
		MIN_EPOLL_PORT,
		MAX_EPOLL_PORT - (STRESS_PROCS_MAX * MAX_SERVERS),
		&epoll_port);
	return set_setting("epoll-port", TYPE_ID_INT, &epoll_port);
}

/*
 *  stress_set_epoll_domain()
 *	set the socket domain option
 */
static int stress_set_epoll_domain(const char *name)
{
	int ret, epoll_domain;

	ret = stress_set_net_domain(DOMAIN_ALL, "epoll-domain",
		name, &epoll_domain);
	set_setting("epoll-domain", TYPE_ID_INT, &epoll_domain);

	switch (epoll_domain) {
	case AF_INET:
	case AF_INET6:
		max_servers = 4;
		break;
	case AF_UNIX:
	default:
		max_servers = 1;
	}

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_epoll_domain,	stress_set_epoll_domain },
	{ OPT_epoll_port,	stress_set_epoll_port },
	{ 0,			NULL }
};

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    NEED_GLIBC(2,3,2)

/*
 * epoll_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT epoll_timer_handler(int sig)
{
	(void)sig;

	/* Cancel timer if we detect no more runs */
	if (!g_keep_stressing_flag) {
		struct itimerspec timer;

		timer.it_value.tv_sec = 0;
		timer.it_value.tv_nsec = 0;
		timer.it_interval.tv_sec = timer.it_value.tv_sec;
		timer.it_interval.tv_nsec = timer.it_value.tv_nsec;

		(void)timer_settime(epoll_timerid, 0, &timer, NULL);
	}
}

/*
 *  epoll_spawn()
 *	spawn a process
 */
static pid_t epoll_spawn(
	const args_t *args,
	epoll_func_t func,
	const int child,
	const pid_t ppid,
	const int epoll_port,
	const int epoll_domain)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		return -1;
	}
	if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		func(args, child, ppid, epoll_port, epoll_domain);
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  epoll_set_fd_nonblock()
 *	set non-blocking mode on fd
 */
static int epoll_set_fd_nonblock(const int fd)
{
	int flags;

	if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
		return -1;
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		return -1;
	return 0;
}

/*
 *  epoll_recv_data()
 *	receive data on fd
 */
static void epoll_recv_data(const int fd)
{
	while (g_keep_stressing_flag) {
		char buf[8192];
		ssize_t n;

		n = recv(fd, buf, sizeof(buf), 0);
		if (n == -1) {
			if (errno != EAGAIN)
				(void)close(fd);
			break;
		} else if (n == 0) {
			(void)close(fd);
			break;
		}
	}
}

/*
 *  epoll_ctl_add()
 *	add fd to epoll list
 */
static int epoll_ctl_add(const int efd, const int fd)
{
	struct epoll_event event;

	(void)memset(&event, 0, sizeof(event));
	event.data.fd = fd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event) < 0)
		return -1;

	return 0;
}

/*
 *  epoll_notification()
 *	handle accept notification on sfd, add
 *	fd's to epoll event list
 */
static int epoll_notification(
	const args_t *args,
	const int efd,
	const int sfd)
{
	for (;;) {
		struct sockaddr saddr;
		socklen_t slen = sizeof(saddr);
		int fd;

		if (!keep_stressing())
			return -1;

		if ((fd = accept(sfd, &saddr, &slen)) < 0) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				/* all incoming connections handled so finish */
				return 0;
			}
			if ((errno == EMFILE) || (errno == ENFILE)) {
				/* out of file descriptors! */
				return 0;
			}
			pr_fail_err("accept");
			return -1;
		}
		/*
		 *  Add non-blocking fd to epoll event list
		 */
		if (epoll_set_fd_nonblock(fd) < 0) {
			pr_fail_err("setting socket to non-blocking");
			(void)close(fd);
			return -1;
		}
		if (epoll_ctl_add(efd, fd) < 0) {
			pr_fail_err("epoll ctl add");
			(void)close(fd);
			return -1;
		}
	}
	return 0;
}

/*
 *  epoll_client()
 *	rapidly try to connect to server(s) and
 *	send a relatively short message
 */
static int epoll_client(
	const args_t *args,
	const pid_t ppid,
	const int epoll_port,
	const int epoll_domain)
{
	int port_counter = 0;
	uint64_t connect_timeouts = 0;
	struct sigevent sev;
	struct itimerspec timer;
	struct sockaddr *addr = NULL;

	if (stress_sighandler(args->name, SIGRTMIN, epoll_timer_handler, NULL) < 0)
		return -1;

	do {
		char buf[4096];
		int fd, saved_errno;
		int retries = 0;
		int ret;
		int port = epoll_port + port_counter +
				(max_servers * args->instance);
		socklen_t addr_len = 0;

		/* Cycle through the servers */
		port_counter = (port_counter + 1) % max_servers;
retry:
		if (!g_keep_stressing_flag)
			break;

		if ((fd = socket(epoll_domain, SOCK_STREAM, 0)) < 0) {
			pr_fail_dbg("socket");
			return -1;
		}

		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &epoll_timerid;
		if (timer_create(CLOCK_REALTIME, &sev, &epoll_timerid) < 0) {
			pr_fail_err("timer_create");
			(void)close(fd);
			return -1;
		}

		/*
		 * Allow 0.25 seconds for connection to occur,
		 * connect can block if the connection table
		 * fills up because we're waiting for TIME-OUTs
		 * to occur on previously closed connections
		 */
		timer.it_value.tv_sec = 0;
		timer.it_value.tv_nsec = 250000000;
		timer.it_interval.tv_sec = timer.it_value.tv_sec;
		timer.it_interval.tv_nsec = timer.it_value.tv_nsec;
		if (timer_settime(epoll_timerid, 0, &timer, NULL) < 0) {
			pr_fail_err("timer_settime");
			(void)close(fd);
			return -1;
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			epoll_domain, port, &addr, &addr_len, NET_ADDR_ANY);

		errno = 0;
		ret = connect(fd, addr, addr_len);
		saved_errno = errno;

		/* No longer need timer */
		if (timer_delete(epoll_timerid) < 0) {
			pr_fail_err("timer_delete");
			(void)close(fd);
			return -1;
		}

		if (ret < 0) {
			switch (saved_errno) {
			case EINTR:
				connect_timeouts++;
				break;
			case ECONNREFUSED: /* No servers yet running */
			case ENOENT:	   /* unix domain not yet created */
				break;
			default:
				pr_dbg("%s: connect failed: %d (%s)\n",
					args->name, saved_errno, strerror(saved_errno));
				break;
			}
			(void)close(fd);
			(void)shim_usleep(100000);	/* Twiddle fingers for a moment */

			retries++;
			if (retries > 1000) {
				/* Sigh, give up.. */
				errno = saved_errno;
				pr_fail_dbg("too many connects");
				return -1;
			}
			goto retry;
		}

		(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));
		if (send(fd, buf, sizeof(buf), 0) < 0) {
			(void)close(fd);
			pr_fail_dbg("send");
			break;
		}
		(void)close(fd);
		(void)shim_sched_yield();
		inc_counter(args);
	} while (keep_stressing());

#if defined(AF_UNIX)
	if (addr && (epoll_domain == AF_UNIX)) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (connect_timeouts)
		pr_dbg("%s: %" PRIu64 " x 0.25 second "
			"connect timeouts, connection table full "
			"(instance %" PRIu32 ")\n",
			args->name, connect_timeouts, args->instance);
	return EXIT_SUCCESS;
}

/*
 *  epoll_server()
 *	wait on connections and read data
 */
static void epoll_server(
	const args_t *args,
	const int child,
	const pid_t ppid,
	const int epoll_port,
	const int epoll_domain)
{
	int efd = -1, sfd = -1, rc = EXIT_SUCCESS;
	int so_reuseaddr = 1;
	int port = epoll_port + child + (max_servers * args->instance);
	struct epoll_event *events = NULL;
	struct sockaddr *addr = NULL;
	socklen_t addr_len = 0;

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((sfd = socket(epoll_domain, SOCK_STREAM, 0)) < 0) {
		pr_fail_err("socket");
		rc = EXIT_FAILURE;
		goto die;
	}
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
			&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail_err("setsockopt");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	stress_set_sockaddr(args->name, args->instance, ppid,
		epoll_domain, port, &addr, &addr_len, NET_ADDR_ANY);

	if (bind(sfd, addr, addr_len) < 0) {
		pr_fail_err("bind");
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (epoll_set_fd_nonblock(sfd) < 0) {
		pr_fail_err("setting socket to non-blocking");
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (listen(sfd, SOMAXCONN) < 0) {
		pr_fail_err("listen");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	/*
	 *  Due to historical reasons we have two ways of
	 *  creating the epoll fd, so randomly select one
	 *  or the other to get more test coverage
	 */
#if defined(HAVE_EPOLL_CREATE1)
	if (mwc1()) {
		efd = epoll_create1(0);	/* flag version */
		if (efd < 0) {
			pr_fail_err("epoll_create1");
			rc = EXIT_FAILURE;
			goto die_close;
		}
	} else {
		efd = epoll_create(1);	/* size version */
		if (efd < 0) {
			pr_fail_err("epoll_create");
			rc = EXIT_FAILURE;
			goto die_close;
		}
	}
#else
	efd = epoll_create(1);	/* size version */
	if (efd < 0) {
		pr_fail_err("epoll_create");
		rc = EXIT_FAILURE;
		goto die_close;
	}
#endif
	if (epoll_ctl_add(efd, sfd) < 0) {
		pr_fail_err("epoll ctl add");
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if ((events = calloc(MAX_EPOLL_EVENTS,
				sizeof(struct epoll_event))) == NULL) {
		pr_fail_err("epoll ctl add");
		rc = EXIT_FAILURE;
		goto die_close;
	}

	do {
		int n, i;
		sigset_t sigmask;

		(void)sigemptyset(&sigmask);
		(void)sigaddset(&sigmask, SIGALRM);

		(void)memset(events, 0, MAX_EPOLL_EVENTS * sizeof(struct epoll_event));
		errno = 0;

		/*
		 * Wait for 100ms for an event, allowing us to
		 * to break out if keep_stressing_flag has been changed.
		 * Note: epoll_wait maps to epoll_pwait in glibc, ho hum.
		 */
		if (mwc1()) {
			n = epoll_wait(efd, events, MAX_EPOLL_EVENTS, 100);
		} else {
			n = epoll_pwait(efd, events, MAX_EPOLL_EVENTS, 100, &sigmask);
		}
		if (n < 0) {
			if (errno != EINTR) {
				pr_fail_err("epoll_wait");
				rc = EXIT_FAILURE;
				goto die_close;
			}
			break;
		}

		for (i = 0; i < n; i++) {
			if ((events[i].events & EPOLLERR) ||
			    (events[i].events & EPOLLHUP) ||
			    (!(events[i].events & EPOLLIN))) {
				/*
				 *  Error has occurred or fd is not
				 *  for reading anymore.. so reap fd
				 */
				(void)close(events[i].data.fd);
			} else if (sfd == events[i].data.fd) {
				/*
				 *  The listening socket has notification(s)
				 *  pending, so handle incoming connections
				 */
				if (epoll_notification(args, efd, sfd) < 0)
					break;
			} else {
				/*
				 *  The fd has data available, so read it
				 */
				epoll_recv_data(events[i].data.fd);
			}
		}
	} while (keep_stressing());

die_close:
	if (efd != -1)
		(void)close(efd);
	if (sfd != -1)
		(void)close(sfd);
die:
#if defined(AF_UNIX)
	if (addr && (epoll_domain == AF_UNIX)) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	free(events);

	_exit(rc);
}

/*
 *  stress_epoll
 *	stress by heavy socket I/O
 */
static int stress_epoll(const args_t *args)
{
	pid_t pids[MAX_SERVERS], ppid = getppid();
	int i, rc = EXIT_SUCCESS;
	int epoll_port = DEFAULT_EPOLL_PORT;
	int epoll_domain = AF_UNIX;

	(void)get_setting("epoll-port", &epoll_port);
	(void)get_setting("epoll-domain", &epoll_domain);

	if (max_servers == 1) {
		pr_dbg("%s: process [%d] using socket port %d\n",
			args->name, args->pid,
			epoll_port + args->instance);
	} else {
		pr_dbg("%s: process [%d] using socket ports %d..%d\n",
			args->name, args->pid,
			epoll_port + (max_servers * args->instance),
			epoll_port + (max_servers * (args->instance + 1)) - 1);
	}

	/*
	 *  Spawn off servers to handle multi port connections.
	 *  The (src address, src port, dst address, dst port) tuple
	 *  is kept in the connection table for a default of 60 seconds
	 *  which means for many fast short connections we can
	 *  fill this table up and new connections get blocked until
	 *  this table empties. One strategy is to reduce TIME_WAIT (not
	 *  good) so the easiest way forward is to just increase the
	 *  number of ports being listened to to increase the tuple
	 *  range and hence allow more connections.  See
	 *  http://vincent.bernat.im/en/blog/2014-tcp-time-wait-state-linux.html
	 *  Typically, we are limited to ~500 connections per second
	 *  on a default Linux configuration.
	 */
	(void)memset(pids, 0, sizeof(pids));
	for (i = 0; i < max_servers; i++) {
		pids[i] = epoll_spawn(args, epoll_server, i, ppid, epoll_port, epoll_domain);
		if (pids[i] < 0) {
			pr_fail_dbg("fork");
			goto reap;
		}
	}

	epoll_client(args, ppid, epoll_port, epoll_domain);
reap:
	for (i = 0; i < max_servers; i++) {
		int status;

		if (pids[i] > 0) {
			(void)kill(pids[i], SIGKILL);
			if (shim_waitpid(pids[i], &status, 0) < 0) {
				pr_fail_dbg("waitpid");
			}
		}
	}

	return rc;
}
stressor_info_t stress_epoll_info = {
	.stressor = stress_epoll,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_epoll_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
