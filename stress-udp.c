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

/* See bugs section of udplite(7) */
#if !defined(SOL_UDPLITE)
#define SOL_UDPLITE		(136)
#endif
#if !defined(UDPLITE_SEND_CSCOV)
#define UDPLITE_SEND_CSCOV	(10)
#endif
#if !defined(UDPLITE_RECV_CSCOV)
#define UDPLITE_RECV_CSCOV	(11)
#endif

static const help_t help[] = {
	{ NULL,	"udp N",	"start N workers performing UDP send/receives " },
	{ NULL,	"udp-ops N",	"stop after N udp bogo operations" },
	{ NULL,	"udp-domain D",	"specify domain, default is ipv4" },
	{ NULL,	"udp-lite",	"use the UDP-Lite (RFC 3828) protocol" },
	{ NULL,	"udp-port P",	"use ports P to P + number of workers - 1" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_udp_port(const char *opt)
{
	int udp_port;

	stress_set_net_port("udp-port", opt,
		MIN_UDP_PORT, MAX_UDP_PORT - STRESS_PROCS_MAX,
		&udp_port);
	return set_setting("udp-port", TYPE_ID_INT, &udp_port);
}

/*
 *  stress_set_udp_domain()
 *	set the udp domain option
 */
static int stress_set_udp_domain(const char *name)
{
	int ret, udp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "udp-domain", name, &udp_domain);
	set_setting("udp-domain", TYPE_ID_INT, &udp_domain);

	return ret;
}

static int stress_set_udp_lite(const char *opt)
{
	bool udp_lite = true;

	(void)opt;
	return set_setting("udp-lite", TYPE_ID_BOOL, &udp_lite);
}

/*
 *  stress_udp
 *	stress by heavy udp ops
 */
static int stress_udp(const args_t *args)
{
	int udp_port = DEFAULT_SOCKET_PORT;
	int udp_domain = AF_INET;
	pid_t pid, ppid = getppid();
	int rc = EXIT_SUCCESS;
	int proto = 0;
#if defined(IPPROTO_UDPLITE)
	bool udp_lite = false;
#endif

	(void)get_setting("udp-port", &udp_port);
	(void)get_setting("udp-domain", &udp_domain);
#if defined(IPPROTO_UDPLITE)
	(void)get_setting("udp-lite", &udp_lite);

 	proto = udp_lite ? IPPROTO_UDPLITE : IPPROTO_UDP;

	if ((proto == IPPROTO_UDPLITE) &&
	    (udp_domain == AF_UNIX)) {
		proto = 0;
		if (args->instance == 0) {
			pr_inf("%s: disabling UDP-Lite as it is not "
				"available for UNIX domain UDP\n",
				args->name);
		}
	}
#endif

	pr_dbg("%s: process [%d] using udp port %d\n",
		args->name, (int)args->pid, udp_port + args->instance);

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr = NULL;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		do {
			char buf[UDP_BUF];
			socklen_t len;
			int fd;
			int j = 0;

			if ((fd = socket(udp_domain, SOCK_DGRAM, proto)) < 0) {
				pr_fail_dbg("socket");
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}
			stress_set_sockaddr(args->name, args->instance, ppid,
				udp_domain, udp_port,
				&addr, &len, NET_ADDR_ANY);
#if defined(IPPROTO_UDPLITE)
			if (proto == IPPROTO_UDPLITE) {
				int val = 8;	/* Just the 8 byte header */
				if (setsockopt(fd, SOL_UDPLITE, UDPLITE_SEND_CSCOV, &val, sizeof(val)) < 0) {
					pr_fail_dbg("setsockopt");
					(void)close(fd);
					(void)kill(getppid(), SIGALRM);
					_exit(EXIT_FAILURE);
				}
			}
#endif
			do {
				size_t i;

				for (i = 16; i < sizeof(buf); i += 16, j++) {
					(void)memset(buf, 'A' + (j % 26), sizeof(buf));
					ssize_t ret = sendto(fd, buf, i, 0, addr, len);
					if (ret < 0) {
						if ((errno == EINTR) || (errno == ENETUNREACH))
							break;
						pr_fail_dbg("sendto");
						break;
					}
				}
			} while (keep_stressing());
			(void)close(fd);
		} while (keep_stressing());

#if defined(AF_UNIX)
		if ((udp_domain == AF_UNIX) && addr) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
			(void)unlink(addr_un->sun_path);
		}
#endif
		/* Inform parent we're all done */
		(void)kill(getppid(), SIGALRM);
		_exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */

		char buf[UDP_BUF];
		int fd, status;
#if !defined(__minix__)
		int so_reuseaddr = 1;
#endif
		socklen_t addr_len = 0;
		struct sockaddr *addr = NULL;

		(void)setpgid(pid, g_pgrp);

		if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(udp_domain, SOCK_DGRAM, proto)) < 0) {
			pr_fail_dbg("socket");
			rc = EXIT_FAILURE;
			goto die;
		}
		stress_set_sockaddr(args->name, args->instance, ppid,
			udp_domain, udp_port,
			&addr, &addr_len, NET_ADDR_ANY);
#if defined(IPPROTO_UDPLITE)
		if (proto == IPPROTO_UDPLITE) {
			int val = 8;	/* Just the 8 byte header */

			if (setsockopt(fd, SOL_UDPLITE, UDPLITE_RECV_CSCOV, &val, sizeof(val)) < 0) {
				pr_fail_dbg("setsockopt");
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif
		if (bind(fd, addr, addr_len) < 0) {
			pr_fail_dbg("bind");
			rc = EXIT_FAILURE;
			goto die_close;
		}
#if !defined(__minix__)
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			/*
			 *  Some systems don't support SO_REUSEADDR
			 */
			if (errno != EINVAL) {
				pr_fail_dbg("setsockopt");
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif

		do {
			socklen_t len = addr_len;
			ssize_t n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail_dbg("recvfrom");
				break;
			}
			inc_counter(args);
		} while (keep_stressing());

die_close:
		(void)close(fd);
die:
#if defined(AF_UNIX)
		if ((udp_domain == AF_UNIX) && addr) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;
			(void)unlink(addr_un->sun_path);
		}
#endif
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		}
	}
	return rc;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_udp_domain,	stress_set_udp_domain },
	{ OPT_udp_port,		stress_set_udp_port },
	{ OPT_udp_lite,		stress_set_udp_lite },
	{ 0,			NULL }
};

stressor_info_t stress_udp_info = {
	.stressor = stress_udp,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
