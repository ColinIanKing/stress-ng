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
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-net.h"
#include "core-pragma.h"

#include <time.h>

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#endif

#if defined(HAVE_SYS_EPOLL_H)
#include <sys/epoll.h>
#endif

#define DEFAULT_EPOLL_PORT	(6000)

#define MAX_EPOLL_EVENTS 	(1024)
#define MAX_SERVERS		(4)
#define MIN_EPOLL_SOCKETS	(64)
#define MAX_EPOLL_SOCKETS	(100000)
#define DEFAULT_EPOLL_SOCKETS	(4096)

static const stress_help_t help[] = {
	{ NULL,	"epoll N",	  	"start N workers doing epoll handled socket activity" },
	{ NULL,	"epoll-domain D", 	"specify socket domain, default is unix" },
	{ NULL,	"epoll-ops N",	  	"stop after N epoll bogo operations" },
	{ NULL,	"epoll-port P",	  	"use socket ports P upwards" },
	{ NULL, "epoll-sockets N",	"specify maximum number of open sockets" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE) &&	\
    defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    NEED_GLIBC(2,3,2)

typedef void (stress_epoll_func_t)(
	stress_args_t *args,
	const int child,
	const pid_t mypid,
	const int epoll_port,
	const int epoll_domain,
	const int epoll_sockets,
	const int max_servers);

static timer_t epoll_timerid;

#endif

static int epoll_domain_mask = DOMAIN_ALL;

static const stress_opt_t opts[] = {
	{ OPT_epoll_domain,  "epoll-domain",  TYPE_ID_INT_DOMAIN, 0, 0, &epoll_domain_mask },
	{ OPT_epoll_port,    "epoll-port",    TYPE_ID_INT_PORT,   MIN_PORT, MAX_PORT, NULL },
	{ OPT_epoll_sockets, "epoll-sockets", TYPE_ID_INT,        MIN_EPOLL_SOCKETS, MAX_EPOLL_SOCKETS, NULL },
	END_OPT,
};

#if defined(HAVE_SYS_EPOLL_H) &&	\
    defined(HAVE_EPOLL_CREATE) &&	\
    defined(HAVE_LIB_RT) &&		\
    defined(HAVE_TIMER_CREATE) &&	\
    defined(HAVE_TIMER_DELETE) &&	\
    defined(HAVE_TIMER_SETTIME) &&	\
    NEED_GLIBC(2,3,2)

static sigjmp_buf jmp_env;

/*
 *  stress_epoll_pwait()
 *	attempt to use epoll_pwait2 (if available) or epoll_pwait
 */
static int stress_epoll_pwait(
	int epfd,
	struct epoll_event *events,
	int maxevents,
	int timeout,
	const sigset_t *sigmask)
{
#if defined(__NR_epoll_pwait2) &&	\
    defined(HAVE_SYSCALL)
	if (stress_mwc1()) {
		struct timespec timeout_ts;
		const int64_t timeout_ns = (int64_t)timeout * 1000;
		int ret;

		timeout_ts.tv_sec = timeout_ns / STRESS_NANOSECOND;
		timeout_ts.tv_nsec = timeout_ns % STRESS_NANOSECOND;

		ret = (int)syscall(__NR_epoll_pwait2, epfd, events,
				   maxevents, &timeout_ts, NULL, 0);
		if (ret == 0)
			return ret;
		if ((ret < 0) && (errno != ENOSYS))
			return ret;
	}
#endif
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
	return epoll_pwait(epfd, events, maxevents, timeout, sigmask);
STRESS_PRAGMA_POP
}

static void NORETURN MLOCKED_TEXT stress_segv_handler(int num)
{
	(void)num;

	siglongjmp(jmp_env, 1);
	stress_no_return();
}

/*
 * epoll_timer_handler()
 *	catch timer signal and cancel if no more runs flagged
 */
static void MLOCKED_TEXT epoll_timer_handler(int sig)
{
	(void)sig;

	/* Cancel timer if we detect no more runs */
	if (UNLIKELY(!stress_continue_flag())) {
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
	stress_args_t *args,
	stress_epoll_func_t func,
	stress_pid_t **s_pids_head,
	stress_pid_t *s_pid,
	const int child,
	const pid_t mypid,
	const int epoll_port,
	const int epoll_domain,
	const int epoll_sockets,
	const int max_servers)
{
again:
	s_pid->pid = fork();
	if (s_pid->pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return -1;
	} else if (s_pid->pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		s_pid->pid = getpid();
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);
		stress_sync_start_wait_s_pid(s_pid);

		func(args, child, mypid, epoll_port, epoll_domain, epoll_sockets, max_servers);
		_exit(EXIT_SUCCESS);
	}  else {
		stress_sync_start_s_pid_list_add(s_pids_head, s_pid);
	}
	return s_pid->pid;
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
static void epoll_recv_data(const int fd, int *fd_count)
{
	while (stress_continue_flag()) {
		char buf[8192];
		ssize_t n;

		n = recv(fd, buf, sizeof(buf), 0);
		if (n < 0) {
			if (errno != EAGAIN) {
				(void)close(fd);
				(*fd_count)--;
			}
			break;
		} else if (n == 0) {
			(void)close(fd);
			(*fd_count)--;
			break;
		}
	}
}

/*
 *  epoll_ctl_add()
 *	add fd to epoll list
 */
static int epoll_ctl_add(const int efd, const int fd, const uint32_t events)
{
	struct epoll_event event;

	(void)shim_memset(&event, 0, sizeof(event));
	event.data.fd = fd;
	event.events = events;
	if (UNLIKELY(epoll_ctl(efd, EPOLL_CTL_ADD, fd, &event) < 0))
		return -1;

	return 0;
}

#if defined(EPOLLEXCLUSIVE)
/*
 *  epoll_ctl_mod()
 *      epoll modify
 */
static int epoll_ctl_mod(const int efd, const int fd, const uint32_t events)
{
	struct epoll_event event;

	(void)shim_memset(&event, 0, sizeof(event));
	event.events = events;
	if (UNLIKELY(epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event) < 0))
		return -1;

	return 0;
}
#endif

/*
 *  epoll_ctl_del()
 *	del fd from epoll list
 */
static int epoll_ctl_del(const int efd, const int fd)
{
	struct epoll_event event;

	(void)shim_memset(&event, 0, sizeof(event));
	if (UNLIKELY(epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event) < 0))
		return -1;

	return 0;
}

/*
 *  epoll_notification()
 *	handle accept notification on sfd, add
 *	fd's to epoll event list
 */
static int epoll_notification(
	stress_args_t *args,
	const int efd,
	const int sfd,
	const int epoll_sockets,
	int *fd_count)
{
	const int bad_fd = stress_get_bad_fd();

	for (;;) {
		struct sockaddr saddr;
		socklen_t slen = sizeof(saddr);
		int fd;
		struct epoll_event event;

		if (UNLIKELY(!stress_continue(args)))
			return -1;
		/* Try to limit too many open fds */
		if (*fd_count > epoll_sockets)
			return 0;

		if (UNLIKELY((fd = accept(sfd, &saddr, &slen)) < 0)) {
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				/* all incoming connections handled so finish */
				break;
			}
			if ((errno == EMFILE) || (errno == ENFILE)) {
				/* out of file descriptors! */
				break;
			}
			if (errno == EINTR) {
				/* interrupted */
				break;
			}
			pr_fail("%s: accept failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		/*
		 *  Add non-blocking fd to epoll event list
		 */
		if (UNLIKELY(epoll_set_fd_nonblock(fd) < 0)) {
			pr_fail("%s: setting socket to non-blocking failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}
		(*fd_count)++;

#if 0
		/*
		 *  Exercise invalid epoll_ctl syscall with EPOLL_CTL_DEL
		 *  and EPOLL_CTL_MOD on fd not registered with efd
		 */
		(void)shim_memset(&event, 0, sizeof(event));
		(void)epoll_ctl(efd, EPOLL_CTL_DEL, fd, &event);
		(void)epoll_ctl(efd, EPOLL_CTL_MOD, fd, &event);
#endif

		if (UNLIKELY(epoll_ctl_add(efd, fd, EPOLLIN) < 0)) {
			pr_fail("%s: epoll_ctl_add failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return -1;
		}

		/*
		 *  Exercise kernel, force add on a bad fd, ignore error
		 */
		(void)epoll_ctl_add(efd, bad_fd, EPOLLIN | EPOLLET);

		/* Exercise epoll_ctl syscall with invalid operation */
		(void)shim_memset(&event, 0, sizeof(event));
		(void)epoll_ctl(efd, INT_MIN, fd, &event);

		/*
		 *  Exercise illegal epoll_ctl_add having fd
		 *  same as efd, resulting in EINVAL error
		 */
		if (UNLIKELY((epoll_ctl_add(efd, efd, EPOLLIN | EPOLLET) == 0) &&
			     (errno == 0))) {
			pr_fail("%s: epoll_ctl_add unexpectedly succeeded with "
				"invalid arguments\n", args->name);
			(void)close(fd);
			return -1;
		}

		/*
		 *  Exercise illegal epoll_ctl_add by adding
		 *  fd which is already registered with efd
		 *  resulting in EEXIST error
		 */
		if (UNLIKELY((epoll_ctl_add(efd, fd, EPOLLIN | EPOLLET) == 0) &&
			     (errno == 0))) {
			pr_fail("%s: epoll_ctl_add unexpectedly succeeded "
				"with a file descriptor that has already "
				"been registered\n", args->name);
			(void)close(fd);
			return -1;
		}

		/*
		 *  Exercise epoll_ctl_add on a illegal epoll_fd
		 */
		if (UNLIKELY((epoll_ctl_add(-1, fd, EPOLLIN | EPOLLET) == 0) &&
			     (errno == 0))) {
			pr_fail("%s: epoll_ctl_add unexpectedly succeeded "
				"with an illegal file descriptor\n", args->name);
			(void)close(fd);
		}
	}
	return 0;
}

/*
 * test_eloop()
 *	test if EPOLL_CTL_ADD operation resulting in
 *	a circular loop of epoll instances monitoring
 *	one another could not succeed.
 */
static int test_eloop(
	stress_args_t *args,
	const int efd,
	const int efd2)
{
	int ret;

	ret = epoll_ctl_add(efd, efd2, EPOLLIN | EPOLLET);
	if (UNLIKELY(ret < 0)) {
		pr_fail("%s: epoll_ctl_add failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}

	ret = epoll_ctl_add(efd2, efd, EPOLLIN | EPOLLET);
	if (UNLIKELY(ret == 0)) {
		pr_fail("%s: epoll_ctl_add failed, expected ELOOP, instead got "
			"errno=%d (%s)\n", args->name, errno, strerror(errno));
		(void)epoll_ctl_del(efd2, efd);
		(void)epoll_ctl_del(efd, efd2);
		return -1;
	}
	(void)epoll_ctl_del(efd, efd2);

	return 0;
}

#if defined(EPOLLEXCLUSIVE)
/*
 * test_epoll_exclusive()
 *      tests all EPOLL_CTL operations resulting in
 *      an error due to the EPOLL_EXCLUSIVE event type
 */
static int test_epoll_exclusive(
	stress_args_t *args,
	const int efd,
	const int efd2,
	const int sfd)
{
	int rc = -1;

	/*
	 *  Deleting sfd from efd so that following
	 *  test does not face any interruption
	 */
	(void)epoll_ctl_del(efd, sfd);

	/*
	 *  Invalid epoll_ctl syscall as EPOLLEXCLUSIVE event
	 *  cannot be operated by EPOLL_CTL_MOD operation
	 */
	if (UNLIKELY((epoll_ctl_mod(efd, sfd, EPOLLEXCLUSIVE) == 0) &&
		     (errno == 0))) {
		pr_fail("%s: epoll_ctl failed, expected EINVAL or ENOENT, instead got "
			"errno=%d (%s)\n", args->name, errno, strerror(errno));
		goto err;
	}

	if (UNLIKELY(epoll_ctl_add(efd, sfd, EPOLLEXCLUSIVE) < 0)) {
		pr_fail("%s: epoll_ctl_add failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	/*
	 *  Invalid epoll_ctl syscall as sfd was registered
	 *  as EPOLLEXCLUSIVE event so it can't be modified
	 */
	if (UNLIKELY((epoll_ctl_mod(efd, sfd, 0) == 0) &&
		     (errno == 0))) {
		pr_fail("%s: epoll_ctl failed, expected EINVAL instead got "
			"errno=%d (%s)\n", args->name, errno, strerror(errno));
		goto err;
	}

	/*
	 *  Invalid epoll_ctl syscall as EPOLLEXCLUSIVE was
	 *  specified in event and fd refers to an epoll instance.
	 */
	if (UNLIKELY((epoll_ctl_add(efd, efd2, EPOLLEXCLUSIVE) == 0) &&
		     (errno == 0))) {
		pr_fail("%s: epoll_ctl failed, expected EINVAL, instead got "
			"errno=%d (%s)\n", args->name, errno, strerror(errno));
		goto err;
	}

	rc = 0;
err:
	epoll_ctl_del(efd, sfd);
	if (UNLIKELY(epoll_ctl_add(efd, sfd, EPOLLIN | EPOLLET) < 0))
		rc = -1;

	return rc;
}
#endif

/*
 *  epoll_client()
 *	rapidly try to connect to server(s) and
 *	send a relatively short message
 */
static int epoll_client(
	stress_args_t *args,
	const pid_t mypid,
	const int epoll_port,
	const int epoll_domain,
	const int max_servers)
{
	int port_counter = 0;
	uint64_t connect_timeouts = 0;
	struct sigevent sev;
	struct itimerspec timer;
	struct sockaddr *addr = NULL;
	uint64_t buf[4096 / sizeof(uint64_t)];

	if (stress_sighandler(args->name, SIGRTMIN, epoll_timer_handler, NULL) < 0)
		return EXIT_FAILURE;

	stress_rndbuf((void *)buf, sizeof(buf));

	do {
		int fd, saved_errno;
#if defined(STRESS_EPOLL_RETRY_COUNT)
		int retries = 0;
#endif
		int ret;
		const int port = epoll_port + port_counter +
				(max_servers * (int)args->instance);
		socklen_t addr_len = 0;

		/* Cycle through the servers */
		port_counter = 0;
		if (port_counter >= max_servers)
			port_counter = 0;
retry:
		if (UNLIKELY(!stress_continue_flag()))
			break;

		if (UNLIKELY((fd = socket(epoll_domain, SOCK_STREAM, 0)) < 0)) {
			if ((errno == EMFILE) ||
			    (errno == ENFILE) ||
			    (errno == ENOMEM) ||
			    (errno == ENOBUFS))
				continue;
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		(void)shim_memset(&sev, 0, sizeof(sev));
		sev.sigev_notify = SIGEV_SIGNAL;
		sev.sigev_signo = SIGRTMIN;
		sev.sigev_value.sival_ptr = &epoll_timerid;
		if (UNLIKELY(timer_create(CLOCK_REALTIME, &sev, &epoll_timerid) < 0)) {
			if ((errno == EAGAIN) || (errno == ENOMEM)) {
				(void)close(fd);
				continue;
			}
			pr_fail("%s: timer_create failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
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
		if (UNLIKELY(timer_settime(epoll_timerid, 0, &timer, NULL) < 0)) {
			pr_fail("%s: timer_settime failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}

		if (UNLIKELY(stress_set_sockaddr(args->name, args->instance, mypid,
			epoll_domain, port, &addr, &addr_len, NET_ADDR_ANY) < 0)) {
			(void)close(fd);
			return EXIT_FAILURE;
		}

		errno = 0;
		ret = connect(fd, addr, addr_len);
		saved_errno = errno;

		/* No longer need timer */
		if (UNLIKELY(timer_delete(epoll_timerid) < 0)) {
			pr_fail("%s: timer_delete failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			return EXIT_FAILURE;
		}

		if (UNLIKELY(ret < 0)) {
			switch (saved_errno) {
			case EINTR:
				connect_timeouts++;
				break;
			case ECONNREFUSED: /* No servers yet running */
			case ENOENT:	   /* unix domain not yet created */
				break;
			default:
				pr_dbg("%s: connect failed, errno=%d (%s)\n",
					args->name, saved_errno, strerror(saved_errno));
				break;
			}
			(void)close(fd);
			(void)shim_usleep(100000);	/* Twiddle fingers for a moment */

#if defined(STRESS_EPOLL_RETRY_COUNT)
			retries++;
			if (retries > 10) {
				/* Sigh, give up.. */
				pr_fail("%s: giving up, too many failed connects, errno=%d (%s)\n",
					args->name, saved_errno, strerror(saved_errno));
				return EXIT_FAILURE;
			}
#endif
			goto retry;
		}

		buf[0]++;
		if (UNLIKELY(send(fd, (void *)buf, sizeof(buf), 0) < 0)) {
			(void)close(fd);
			pr_dbg("%s: send failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		(void)close(fd);
		stress_bogo_inc(args);
		if (UNLIKELY(!stress_continue(args)))
			break;
		(void)shim_sched_yield();
	} while (stress_continue(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && (epoll_domain == AF_UNIX)) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
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
static void NORETURN epoll_server(
	stress_args_t *args,
	const int child,
	const pid_t mypid,
	const int epoll_port,
	const int epoll_domain,
	const int epoll_sockets,
	const int max_servers)
{
	NOCLOBBER int efd = -1, efd2 = -1, sfd = -1, rc = EXIT_SUCCESS;
	int so_reuseaddr = 1;
	int port = epoll_port + child + (max_servers * (int)args->instance);
	NOCLOBBER struct epoll_event *events = NULL;
	struct sockaddr *addr = NULL;
	socklen_t addr_len = 0;
	const int bad_fd = stress_get_bad_fd();
	int fd_count = 0;

	if (stress_sighandler(args->name, SIGSEGV, stress_segv_handler, NULL) < 0) {
		rc = EXIT_NO_RESOURCE;
		goto die;
	}
	if ((sfd = socket(epoll_domain, SOCK_STREAM, 0)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die;
	}
	if (setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR,
			&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail("%s: setsockopt SO_REUSEADDR failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	if (stress_set_sockaddr(args->name, args->instance, mypid,
		epoll_domain, port, &addr, &addr_len, NET_ADDR_ANY) < 0) {
		rc = EXIT_FAILURE;
		goto die_close;
	}

	if (bind(sfd, addr, addr_len) < 0) {
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (epoll_set_fd_nonblock(sfd) < 0) {
		pr_fail("%s: setting socket to non-blocking failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
	if (listen(sfd, SOMAXCONN) < 0) {
		pr_fail("%s: listen failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	/*
	 *  Due to historical reasons we have two ways of
	 *  creating the epoll fd, so randomly select one
	 *  or the other to get more test coverage
	 */
#if defined(HAVE_EPOLL_CREATE1)
	if (stress_mwc1()) {
		/* Invalid epoll_create1 syscall with invalid flag */
		efd = epoll_create1(INT_MIN);
		if (efd >= 0) {
			(void)close(efd);
			pr_fail("%s: epoll_create1 unexpectedly succeeded with an invalid flag, "
				"errno=%d (%s)\n", args->name, errno, strerror(errno));
		}

		/* Exercise epoll_create1 syscall with non-zero flag */
		efd = epoll_create1(EPOLL_CLOEXEC);
		if (efd < 0) {
			pr_fail("%s: epoll_create1 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		} else {
			(void)close(efd);
		}

		efd = epoll_create1(0);	/* flag version */
		if (efd < 0) {
			pr_fail("%s: epoll_create1 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
		efd2 = epoll_create1(0);	/* flag version */
		if (efd2 < 0) {
			pr_fail("%s: epoll_create1 failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
	} else {
		/* Invalid epoll_create syscall with invalid size */
		efd = epoll_create(INT_MIN);
		if (efd >= 0) {
			(void)close(efd);
			pr_fail("%s: epoll_create unexpectedly succeeded with an invalid size, "
				"errno=%d (%s)\n", args->name, errno, strerror(errno));
		}

		efd = epoll_create(1);	/* size version */
		if (efd < 0) {
			pr_fail("%s: epoll_create failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
		efd2 = epoll_create(1);	/* size version */
		if (efd2 < 0) {
			pr_fail("%s: epoll_create failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
	}
#else
	/* Invalid epoll_create syscall with invalid size */
	efd = epoll_create(INT_MIN);
	if (efd >= 0) {
		(void)close(efd);
		pr_fail("%s: epoll_create unexpectedly succeeded with an invalid size, "
			"errno=%d (%s)\n", args->name, errno, strerror(errno));
	}

	efd = epoll_create(1);	/* size version */
	if (efd < 0) {
		pr_fail("%s: epoll_create failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
	efd2 = epoll_create(1);	/* size version */
	if (efd2 < 0) {
		pr_fail("%s: epoll_create failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
#endif
	if (epoll_ctl_add(efd, sfd, EPOLLIN | EPOLLET) < 0) {
		pr_fail("%s: epoll_ctl_add failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}
	events = (struct epoll_event *)calloc(MAX_EPOLL_EVENTS, sizeof(*events));
	if (!events) {
		pr_fail("%s: calloc of %d events failed%s, out of memory\n",
			args->name, MAX_EPOLL_EVENTS, stress_get_memfree_str());
		rc = EXIT_FAILURE;
		goto die_close;
	}

	do {
		int n, i, ret, saved_errno;
		sigset_t sigmask;
		static bool wait_segv = false;

		(void)sigemptyset(&sigmask);
		(void)sigaddset(&sigmask, SIGALRM);

		(void)shim_memset(events, 0, MAX_EPOLL_EVENTS * sizeof(*events));
		errno = 0;

		ret = sigsetjmp(jmp_env, 1);
		if (UNLIKELY(!stress_continue(args)))
			break;
		if (UNLIKELY(ret != 0))
			wait_segv = true;

		/*
		 * Wait for 100ms for an event, allowing us to
		 * to break out if stress_continue_flag has been changed.
		 * Note: epoll_wait maps to epoll_pwait in glibc, ho hum.
		 */
		if (stress_mwc1()) {
			if (!wait_segv) {
				/*
				 *  Exercise an unmapped page for the events buffer, it should
				 *  never return more than 0 events and if it does we were expecting
				 *  -EFAULT.
				 */
				n = epoll_wait(efd, args->mapped->page_none, 1, 100);
				if (UNLIKELY(n > 0)) {
					pr_fail("%s: epoll_wait unexpectedly succeeded, "
						"expected -EFAULT, instead got errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
			n = epoll_wait(efd, events, MAX_EPOLL_EVENTS, 100);
			saved_errno = errno;

			/* Invalid epoll_wait syscall having invalid maxevents argument */
			(void)epoll_wait(efd, events, 0, 100);

		} else {
			if (!wait_segv) {
				/*
				 *  Exercise an unmapped page for the events buffer, it should
				 *  never return more than 0 events and if it does we were expecting
				 *  -EFAULT.
				 */
				n = stress_epoll_pwait(efd, args->mapped->page_none, 1, 100, &sigmask);
				if (UNLIKELY(n > 1)) {
					pr_fail("%s: epoll_pwait unexpectedly succeeded, "
						"expected -EFAULT, instead got errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
			}
			n = stress_epoll_pwait(efd, events, MAX_EPOLL_EVENTS, 100, &sigmask);
			saved_errno = errno;

			/* Invalid epoll_pwait syscall having invalid maxevents argument */
			(void)stress_epoll_pwait(efd, events, INT_MIN, 100, &sigmask);

		}
		if (UNLIKELY(n < 0)) {
			if ((saved_errno != EINTR) &&
			    (saved_errno != EINVAL)) {
				pr_fail("%s: epoll_wait failed, errno=%d (%s)\n",
					args->name, saved_errno, strerror(saved_errno));
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
				fd_count--;
			} else if (sfd == events[i].data.fd) {
				/*
				 *  The listening socket has notification(s)
				 *  pending, so handle incoming connections
				 */
				if (UNLIKELY(epoll_notification(args, efd, sfd, epoll_sockets, &fd_count) < 0))
					break;
				if (UNLIKELY(test_eloop(args, efd, efd2) < 0))
					break;
#if defined(EPOLLEXCLUSIVE)
				if (UNLIKELY(test_epoll_exclusive(args, efd, efd2, sfd) < 0))
					break;
#endif
			} else {
				/*
				 *  The fd has data available, so read it
				 */
				epoll_recv_data(events[i].data.fd, &fd_count);
			}
		}
		/*
		 *  Exercise kernel on epoll wait with a bad fd, ignore error
		 */
		if (stress_mwc1()) {
			n = epoll_wait(bad_fd, events, MAX_EPOLL_EVENTS, 100);
			(void)n;
		} else {
			n = stress_epoll_pwait(bad_fd, events, MAX_EPOLL_EVENTS, 100, &sigmask);
			(void)n;
		}
	} while (stress_continue(args));

die_close:
	if (efd > -1)
		(void)close(efd);
	if (efd2 > -1)
		(void)close(efd2);
	if (sfd > -1)
		(void)close(sfd);
die:
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && (epoll_domain == AF_UNIX)) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	free(events);

	_exit(rc);
}

/*
 *  stress_epoll
 *	stress by heavy socket I/O
 */
static int stress_epoll(stress_args_t *args)
{
	stress_pid_t *s_pids, *s_pids_head = NULL;
	pid_t mypid = getpid();
	int i, rc = EXIT_SUCCESS;
	int epoll_domain = AF_UNIX;
	int epoll_port = DEFAULT_EPOLL_PORT;
	int epoll_sockets = DEFAULT_EPOLL_SOCKETS;
	int start_port, end_port, reserved_port;
	int max_servers;

	(void)stress_get_setting("epoll-domain", &epoll_domain);
	(void)stress_get_setting("epoll-port", &epoll_port);
	if (!stress_get_setting("epoll-sockets", &epoll_sockets)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			epoll_sockets = MAX_EPOLL_SOCKETS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			epoll_sockets = MIN_EPOLL_SOCKETS;
	}

	if (stress_sighandler(args->name, SIGPIPE, SIG_IGN, NULL) < 0)
		return EXIT_NO_RESOURCE;

	s_pids = stress_sync_s_pids_mmap(MAX_SERVERS);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, MAX_SERVERS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	switch (epoll_domain) {
	case AF_INET:
	case AF_INET6:
		max_servers = 4;
		break;
	case AF_UNIX:
	default:
		max_servers = 1;
	}

	if (max_servers == 1) {
		start_port = epoll_port + (int)args->instance;
		if (start_port > MAX_PORT)
			start_port -= (MAX_PORT - MIN_PORT + 1);
		reserved_port = stress_net_reserve_ports(start_port, start_port);
		if (reserved_port < 0) {
			pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
				args->name, start_port);
			(void)stress_sync_s_pids_munmap(s_pids, MAX_SERVERS);
			return EXIT_NO_RESOURCE;
		}
		/* adjust for reserved port range */
		start_port = reserved_port;
		end_port = reserved_port;
		epoll_port = start_port - (int)args->instance;

		pr_dbg("%s: process [%" PRIdMAX "] using socket port %d\n",
			args->name, (intmax_t)args->pid,
			epoll_port + (int)args->instance);
	} else {
		start_port = epoll_port + (max_servers * (int)args->instance);
		end_port = start_port + max_servers - 1;
		if (end_port > MAX_PORT) {
			start_port = MIN_PORT;
			end_port = start_port + max_servers - 1;
		}

		reserved_port = stress_net_reserve_ports(start_port, end_port);
		if (reserved_port < 0) {
			pr_inf_skip("%s: cannot reserve ports %d..%d, skipping stressor\n",
				args->name, start_port, end_port);
			(void)stress_sync_s_pids_munmap(s_pids, MAX_SERVERS);
			return EXIT_NO_RESOURCE;
		}
		/* adjust for reserved port range */
		start_port = reserved_port;
		end_port = reserved_port + max_servers - 1;
		epoll_port = start_port - (max_servers * (int)args->instance);

		pr_dbg("%s: process [%" PRIdMAX "] using socket ports %d..%d\n",
			args->name, (intmax_t)args->pid, start_port, end_port);
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
	for (i = 0; i < max_servers; i++) {
		stress_sync_start_init(&s_pids[i]);
		if (epoll_spawn(args, epoll_server, &s_pids_head, &s_pids[i], i, mypid, epoll_port, epoll_domain, epoll_sockets, max_servers) < 0) {
			pr_fail("%s: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto reap;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = epoll_client(args, mypid, epoll_port, epoll_domain, max_servers);
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(start_port, end_port);

	stress_kill_and_wait_many(args, s_pids, max_servers, SIGALRM, true);
	(void)stress_sync_s_pids_munmap(s_pids, MAX_SERVERS);

	return rc;
}
const stressor_info_t stress_epoll_info = {
	.stressor = stress_epoll,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_epoll_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/epoll.h or librt or timer support"
};
#endif
