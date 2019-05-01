/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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

#define DCCP_OPT_SEND		0x01
#define DCCP_OPT_SENDMSG	0x02
#define DCCP_OPT_SENDMMSG	0x03

#define MSGVEC_SIZE		(4)

typedef struct {
	const char *optname;
	int	   opt;
} dccp_opts_t;

static const help_t help[] = {
	{ NULL,	"dccp N",		"start N workers exercising network DCCP I/O" },
	{ NULL,	"dccp-domain D",	"specify DCCP domain, default is ipv4" },
	{ NULL,	"dccp-ops N",		"stop after N DCCP  bogo operations" },
	{ NULL,	"dccp-opts option",	"DCCP data send options [send|sendmsg|sendmmsg]" },
	{ NULL,	"dccp-port P",		"use DCCP ports P to P + number of workers - 1" },
	{ NULL,	NULL,			NULL }
};

static const dccp_opts_t dccp_options[] = {
	{ "send",	DCCP_OPT_SEND },
	{ "sendmsg",	DCCP_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
	{ "sendmmsg",	DCCP_OPT_SENDMMSG },
#endif
	{ NULL,		0 }
};

/*
 *  stress_set_dccp_opts()
 *	parse --dccp-opts
 */
static int stress_set_dccp_opts(const char *opt)
{
	size_t i;

	for (i = 0; dccp_options[i].optname; i++) {
		if (!strcmp(opt, dccp_options[i].optname)) {
			int dccp_opt = dccp_options[i].opt;

			set_setting("dccp-opts", TYPE_ID_INT, &dccp_opt);
			return 0;
		}
	}
	(void)fprintf(stderr, "dccp-opts option '%s' not known, options are:", opt);
	for (i = 0; dccp_options[i].optname; i++) {
		(void)fprintf(stderr, "%s %s",
			i == 0 ? "" : ",", dccp_options[i].optname);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

/*
 *  stress_set_dccp_port()
 *	set port to use
 */
static int stress_set_dccp_port(const char *opt)
{
	int dccp_port;

	stress_set_net_port("dccp-port", opt,
		MIN_DCCP_PORT, MAX_DCCP_PORT - STRESS_PROCS_MAX,
		&dccp_port);
	return set_setting("dccp-port", TYPE_ID_INT, &dccp_port);
}

/*
 *  stress_set_dccp_domain()
 *	set the socket domain option
 */
static int stress_set_dccp_domain(const char *name)
{
	int ret, dccp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6,
				"dccp-domain", name, &dccp_domain);
	set_setting("dccp-domain", TYPE_ID_INT, &dccp_domain);
	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_dccp_domain,	stress_set_dccp_domain },
	{ OPT_dccp_opts,	stress_set_dccp_opts },
	{ OPT_dccp_port,	stress_set_dccp_port },
	{ 0,			NULL },
};

#if defined(SOCK_DCCP) && defined(IPPROTO_DCCP)

/*
 *  stress_dccp_client()
 *	client reader
 */
static void stress_dccp_client(
	const args_t *args,
	const pid_t ppid,
	const int dccp_port,
	const int dccp_domain)
{
	struct sockaddr *addr;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();

	do {
		char buf[DCCP_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
retry:
		if (!g_keep_stressing_flag) {
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}
		if ((fd = socket(dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
			if (errno == ESOCKTNOSUPPORT) {
				/*
				 *  Protocol not supported - then return
				 *  EXIT_NOT_IMPLEMENTED and skip the test
				 */
				_exit(EXIT_NOT_IMPLEMENTED);
			}
			pr_fail_dbg("socket");
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		stress_set_sockaddr(args->name, args->instance, ppid,
			dccp_domain, dccp_port,
			&addr, &addr_len, NET_ADDR_ANY);
		if (connect(fd, addr, addr_len) < 0) {
			int err = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				errno = err;
				pr_fail_dbg("connect");
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			goto retry;
		}

		do {
			ssize_t n = recv(fd, buf, sizeof(buf), 0);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail_dbg("recv");
				break;
			}
		} while (keep_stressing());
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (keep_stressing());

#if defined(AF_UNIX)
	if (dccp_domain == AF_UNIX) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	/* Inform parent we're all done */
	(void)kill(getppid(), SIGALRM);
}

/*
 *  stress_dccp_server()
 *	server writer
 */
static int stress_dccp_server(
	const args_t *args,
	const pid_t pid,
	const pid_t ppid,
	const int dccp_port,
	const int dccp_domain,
	const int dccp_opts)
{
	char buf[DCCP_BUF];
	int fd, status;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	int rc = EXIT_SUCCESS;

	(void)setpgid(pid, g_pgrp);

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
		if (errno == ESOCKTNOSUPPORT) {
			/*
			 *  Protocol not supported - then return
			 *  EXIT_NOT_IMPLEMENTED and skip the test
			 */
			if (args->instance == 0)
				pr_inf("%s: DCCP protocol not supported, "
					"skipping stressor\n", args->name);
			return EXIT_NOT_IMPLEMENTED;
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
		dccp_domain, dccp_port,
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
			size_t i, j;
			struct sockaddr saddr;
			socklen_t len;
			int sndbuf;
			struct msghdr msg;
			struct iovec vec[sizeof(buf)/16];
#if defined(HAVE_SENDMMSG)
			struct mmsghdr msgvec[MSGVEC_SIZE];
			unsigned int msg_len = 0;
#endif
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

			(void)memset(buf, 'A' + (get_counter(args) % 26), sizeof(buf));
			switch (dccp_opts) {
			case DCCP_OPT_SEND:
				for (i = 16; i < sizeof(buf); i += 16) {
					ssize_t ret;
again:
					ret = send(sfd, buf, i, 0);
					if (ret < 0) {
						if (errno == EAGAIN)
							goto again;
						if (errno != EINTR)
							pr_fail_dbg("send");
						break;
					} else
						msgs++;
				}
				break;
			case DCCP_OPT_SENDMSG:
				for (j = 0, i = 16; i < sizeof(buf); i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
				}
				(void)memset(&msg, 0, sizeof(msg));
				msg.msg_iov = vec;
				msg.msg_iovlen = j;
				if (sendmsg(sfd, &msg, 0) < 0) {
					if (errno != EINTR)
						pr_fail_dbg("sendmsg");
				} else
					msgs += j;
				break;
#if defined(HAVE_SENDMMSG)
			case DCCP_OPT_SENDMMSG:
				(void)memset(msgvec, 0, sizeof(msgvec));
				for (j = 0, i = 16; i < sizeof(buf); i += 16, j++) {
					vec[j].iov_base = buf;
					vec[j].iov_len = i;
					msg_len += i;
				}
				for (i = 0; i < MSGVEC_SIZE; i++) {
					msgvec[i].msg_hdr.msg_iov = vec;
					msgvec[i].msg_hdr.msg_iovlen = j;
				}
				if (sendmmsg(sfd, msgvec, MSGVEC_SIZE, 0) < 0) {
					if (errno != EINTR)
						pr_fail_dbg("sendmmsg");
				} else
					msgs += (MSGVEC_SIZE * j);
				break;
#endif
			default:
				/* Should never happen */
				pr_err("%s: bad option %d\n", args->name, dccp_opts);
				(void)close(sfd);
				goto die_close;
			}
			if (getpeername(sfd, &saddr, &len) < 0) {
				pr_fail_dbg("getpeername");
			}
			(void)close(sfd);
		}
		inc_counter(args);
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX)
	if (addr && (dccp_domain == AF_UNIX)) {
		struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
		(void)unlink(addr_un->sun_path);
	}
#endif
	if (pid) {
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	return rc;
}

/*
 *  stress_dccp
 *	stress by heavy dccp  I/O
 */
static int stress_dccp(const args_t *args)
{
	pid_t pid, ppid = getppid();
	int dccp_port = DEFAULT_DCCP_PORT;
	int dccp_domain = AF_INET;
	int dccp_opts = DCCP_OPT_SEND;

	(void)get_setting("dccp-port", &dccp_port);
	(void)get_setting("dccp-domain", &dccp_domain);
	(void)get_setting("dccp-opts", &dccp_opts);

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, dccp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag &&
		    ((errno == EAGAIN) || (errno == ENOMEM)))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		stress_dccp_client(args, ppid, dccp_port, dccp_domain);
		_exit(EXIT_SUCCESS);
	} else {
		return stress_dccp_server(args, pid, ppid, dccp_port,
			dccp_domain, dccp_opts);
	}
}

stressor_info_t stress_dccp_info = {
	.stressor = stress_dccp,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_dccp_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
