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
	{ NULL,	"sctp N",	 "start N workers performing SCTP send/receives " },
	{ NULL,	"sctp-ops N",	 "stop after N SCTP bogo operations" },
	{ NULL,	"sctp-domain D", "specify sctp domain, default is ipv4" },
	{ NULL,	"sctp-port P",	 "use SCTP ports P to P + number of workers - 1" },
	{ NULL, "sctp-sched S",	 "specify sctp scheduler" },
	{ NULL,	NULL, 		 NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#if !defined(LOCALTIME_STREAM)
#define LOCALTIME_STREAM        0
#endif

static uint64_t	sigpipe_count;
#endif

/*
 *  stress_set_sctp_port()
 *	set port to use
 */
static int stress_set_sctp_port(const char *opt)
{
	int sctp_port;

	stress_set_net_port("sctp-port", opt,
		MIN_SCTP_PORT, MAX_SCTP_PORT - STRESS_PROCS_MAX,
		&sctp_port);
	return set_setting("sctp-port", TYPE_ID_INT, &sctp_port);
}

/*
 *  stress_set_sctp_domain()
 *	set the socket domain option
 */
static int stress_set_sctp_domain(const char *name)
{
	int ret, sctp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "sctp-domain",
				     name, &sctp_domain);
	set_setting("sctp-domain", TYPE_ID_INT, &sctp_domain);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sctp_port,	stress_set_sctp_port },
	{ OPT_sctp_domain,	stress_set_sctp_domain },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#define STRESS_SCTP_SOCKOPT(opt, type)			\
{							\
	type info;					\
	socklen_t opt_len = sizeof(info);		\
	int ret;					\
							\
	ret = getsockopt(fd, IPPROTO_SCTP, opt,		\
		 &info, &opt_len);			\
	if (ret == 0) {					\
		ret = setsockopt(fd, IPPROTO_SCTP, opt,	\
			&info, opt_len);		\
	}						\
}

/*
 *  stress_sctp_sockopts()
 *	exercise some SCTP specific sockopts
 */
static void stress_sctp_sockopts(const int fd)
{
#if defined(SCTP_RTOINFO)
	STRESS_SCTP_SOCKOPT(SCTP_RTOINFO, struct sctp_rtoinfo)
#endif
#if defined(SCTP_ASSOCINFO)
	STRESS_SCTP_SOCKOPT(SCTP_ASSOCINFO, struct sctp_assocparams)
#endif
#if defined(SCTP_INITMSG)
	STRESS_SCTP_SOCKOPT(SCTP_INITMSG, struct sctp_initmsg)
#endif
#if defined(SCTP_NODELAY)
	STRESS_SCTP_SOCKOPT(SCTP_NODELAY, int)
#endif
#if defined(SCTP_PRIMARY_ADDR)
	STRESS_SCTP_SOCKOPT(SCTP_PRIMARY_ADDR, struct sctp_prim)
#endif
#if defined(SCTP_PEER_ADDR_PARAMS)
	STRESS_SCTP_SOCKOPT(SCTP_PEER_ADDR_PARAMS, struct sctp_paddrparams)
#endif
#if defined(SCTP_EVENTS)
	STRESS_SCTP_SOCKOPT(SCTP_EVENTS, struct sctp_event_subscribe)
#endif
#if defined(SCTP_MAXSEG)
	STRESS_SCTP_SOCKOPT(SCTP_MAXSEG, struct sctp_assoc_value)
#endif
#if defined(SCTP_STATUS)
	STRESS_SCTP_SOCKOPT(SCTP_STATUS, struct sctp_status)
#endif
#if defined(SCTP_GET_PEER_ADDR_INFO) && 0
	STRESS_SCTP_SOCKOPT(SCTP_GET_PEER_ADDR_INFO, struct sctp_paddrinfo)
#endif
#if defined(SCTP_GET_ASSOC_STATS)
	STRESS_SCTP_SOCKOPT(SCTP_GET_ASSOC_STATS, struct sctp_assoc_stats)
#endif
}

/*
 *  stress_sctp_client()
 *	client reader
 */
static void stress_sctp_client(
	const args_t *args,
	const pid_t ppid,
	const int sctp_port,
	const int sctp_domain)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
		struct sctp_event_subscribe events;
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
			if (errno == EPROTONOSUPPORT) {
				pr_inf("%s: SCTP protocol not supported, skipping stressor\n",
					args->name);
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_NOT_IMPLEMENTED);
			}
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			sctp_domain, sctp_port,
			&addr, &addr_len, NET_ADDR_LOOPBACK);
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
		(void)memset(&events, 0, sizeof(events));
		events.sctp_data_io_event = 1;
		if (setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events,
			sizeof(events)) < 0) {
			(void)close(fd);
			pr_fail_dbg("setsockopt");
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		do {
			int flags;
			struct sctp_sndrcvinfo sndrcvinfo;
			ssize_t n;

			n = sctp_recvmsg(fd, buf, sizeof(buf),
				NULL, 0, &sndrcvinfo, &flags);
			if (n <= 0)
				break;
		} while (keep_stressing());
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

#if defined(AF_UNIX)
	if (sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int stress_sctp_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int sctp_port,
	const int sctp_domain)
{
	char buf[SOCKET_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_inf("%s: SCTP protocol not supported, skipping stressor\n",
				args->name);
			rc = EXIT_NOT_IMPLEMENTED;
			goto die;
		}
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
		sctp_domain, sctp_port, &addr, &addr_len, NET_ADDR_ANY);
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
			size_t i;

#if defined(SOCKET_NODELAY)
			int one = 1;

			if (opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
				if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
					pr_inf("%s: setsockopt TCP_NODELAY "
						"failed and disabled, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
				}
			}
#endif

			(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));

			for (i = 16; i < sizeof(buf); i += 16) {
				ssize_t ret = sctp_sendmsg(sfd, buf, i,
						NULL, 0, 0, 0,
						LOCALTIME_STREAM, 0, 0);
				if (ret < 0)
					break;
				else {
					inc_counter(args);
					msgs++;
				}
			}
			stress_sctp_sockopts(sfd);
			(void)close(sfd);
		}
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX)
	if (addr && sctp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	return rc;
}

static void stress_sctp_sigpipe(int signum)
{
	(void)signum;

	sigpipe_count++;
}

/*
 *  stress_sctp
 *	stress SCTP by heavy SCTP network I/O
 */
static int stress_sctp(const args_t *args)
{
	pid_t pid, ppid = getppid();
	int sctp_port = DEFAULT_SCTP_PORT;
	int sctp_domain = AF_INET;
	int ret = EXIT_FAILURE;

	(void)get_setting("sctp-port", &sctp_port);
	(void)get_setting("sctp-domain", &sctp_domain);

	if (stress_sighandler(args->name, SIGPIPE, stress_sctp_sigpipe, NULL) < 0)
		return EXIT_FAILURE;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, args->pid, sctp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_sctp_client(args, ppid,
			sctp_port, sctp_domain);
		_exit(EXIT_SUCCESS);
	} else {
		ret = stress_sctp_server(args, pid, ppid,
			sctp_port, sctp_domain);
	}

	if (sigpipe_count)
		pr_dbg("%s: caught %" PRIu64 " SIGPIPE signals\n", args->name, sigpipe_count);

	return ret;
}

stressor_info_t stress_sctp_info = {
	.stressor = stress_sctp,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sctp_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
