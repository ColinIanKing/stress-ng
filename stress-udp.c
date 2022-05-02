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
#include "core-cpu.h"
#include "core-net.h"

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_UDP_H)
#include <linux/udp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#include <netinet/in.h>

#define MIN_UDP_PORT		(1024)
#define MAX_UDP_PORT		(65535)
#define DEFAULT_UDP_PORT	(7000)

#define UDP_BUF			(1024)	/* UDP I/O buffer size */

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

static const stress_help_t help[] = {
	{ NULL,	"udp N",	"start N workers performing UDP send/receives " },
	{ NULL,	"udp-ops N",	"stop after N udp bogo operations" },
	{ NULL,	"udp-domain D",	"specify domain, default is ipv4" },
	{ NULL, "udp-gro",	"enable UDP-GRO" },
	{ NULL,	"udp-lite",	"use the UDP-Lite (RFC 3828) protocol" },
	{ NULL,	"udp-port P",	"use ports P to P + number of workers - 1" },
	{ NULL,	"udp-if I",	"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	NULL,		NULL }
};

static int stress_set_udp_port(const char *opt)
{
	int udp_port;

	stress_set_net_port("udp-port", opt,
		MIN_UDP_PORT, MAX_UDP_PORT - STRESS_PROCS_MAX,
		&udp_port);
	return stress_set_setting("udp-port", TYPE_ID_INT, &udp_port);
}

/*
 *  stress_set_udp_domain()
 *	set the udp domain option
 */
static int stress_set_udp_domain(const char *name)
{
	int ret, udp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "udp-domain", name, &udp_domain);
	stress_set_setting("udp-domain", TYPE_ID_INT, &udp_domain);

	return ret;
}

static int stress_set_udp_lite(const char *opt)
{
	bool udp_lite = true;

	(void)opt;
	return stress_set_setting("udp-lite", TYPE_ID_BOOL, &udp_lite);
}

static int stress_set_udp_gro(const char *opt)
{
	bool udp_gro = true;

	(void)opt;
	return stress_set_setting("udp-gro", TYPE_ID_BOOL, &udp_gro);
}

static int stress_set_udp_if(const char *name)
{
	stress_set_setting("udp-if", TYPE_ID_STR, name);

	return 0;
}

/*
 *  stress_udp
 *	stress by heavy udp ops
 */
static int stress_udp(const stress_args_t *args)
{
	int udp_port = DEFAULT_UDP_PORT;
	int udp_domain = AF_INET;
	pid_t pid, ppid = getppid();
	int rc = EXIT_SUCCESS;
	int proto = 0;
#if defined(IPPROTO_UDPLITE)
	bool udp_lite = false;
#endif
#if defined(UDP_GRO)
	bool udp_gro = false;
#endif
	char *udp_if = NULL;

	(void)stress_get_setting("udp-if", &udp_if);
	(void)stress_get_setting("udp-port", &udp_port);
	(void)stress_get_setting("udp-domain", &udp_domain);
#if defined(IPPROTO_UDPLITE)
	(void)stress_get_setting("udp-lite", &udp_lite);

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

#if defined(UDP_GRO)
	(void)stress_get_setting("udp-gro", &udp_gro);
#endif
	if (udp_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(udp_if, udp_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, udp_if, stress_net_domain(udp_domain));
			udp_if = NULL;
		}
	}

	pr_dbg("%s: process [%d] using udp port %d\n",
		args->name, (int)args->pid, udp_port + (int)args->instance);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		struct sockaddr *addr = NULL;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		do {
			char buf[UDP_BUF];
			socklen_t len;
			int fd;
			int j = 0;

			if ((fd = socket(udp_domain, SOCK_DGRAM, proto)) < 0) {
				pr_fail("%s: socket failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				/* failed, kick parent to finish */
				(void)kill(getppid(), SIGALRM);
				_exit(EXIT_FAILURE);
			}

			stress_set_sockaddr_if(args->name, args->instance, ppid,
				udp_domain, udp_port, udp_if,
				&addr, &len, NET_ADDR_ANY);
#if defined(IPPROTO_UDPLITE) &&	\
    defined(UDPLITE_SEND_CSCOV)
			if (proto == IPPROTO_UDPLITE) {
				int val = 8;	/* Just the 8 byte header */
				socklen_t slen;

				slen = sizeof(val);
				if (setsockopt(fd, SOL_UDPLITE, UDPLITE_SEND_CSCOV, &val, slen) < 0) {
					pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					(void)close(fd);
					(void)kill(getppid(), SIGALRM);
					_exit(EXIT_FAILURE);
				}
				slen = sizeof(val);
				(void)getsockopt(fd, SOL_UDPLITE, UDPLITE_SEND_CSCOV, &val, &slen);
			}
#endif
#if defined(IPPROTO_UDPLITE) &&	\
    defined(UDPLITE_RECV_CSCOV)
			if (proto == IPPROTO_UDPLITE) {
				int val;
				socklen_t slen = sizeof(val);

				(void)getsockopt(fd, proto, UDPLITE_RECV_CSCOV, &val, &slen);
			}
#endif

#if defined(UDP_GRO)
			if (udp_gro) {
				int val, ret;
				socklen_t slen = sizeof(val);

				val = 1;
				ret = setsockopt(fd, proto, UDP_GRO, &val, slen);
				(void)ret;
			}
#endif

#if defined(UDP_CORK)
			{
				int val, ret;
				socklen_t slen = sizeof(val);

				ret = getsockopt(fd, proto, UDP_CORK, &val, &slen);
				if (ret == 0) {
					slen = sizeof(val);
					ret = setsockopt(fd, proto, UDP_CORK, &val, slen);
				}
				(void)ret;
			}
#else
			UNEXPECTED
#endif
#if defined(UDP_ENCAP)
			{
				int val, ret;
				socklen_t slen = sizeof(val);

				ret = getsockopt(fd, proto, UDP_ENCAP, &val, &slen);
				if (ret == 0) {
					slen = sizeof(val);
					ret = setsockopt(fd, proto, UDP_ENCAP, &val, slen);
				}
				(void)ret;
			}
#else
			UNEXPECTED
#endif
#if defined(UDP_NO_CHECK6_TX)
			{
				int val, ret;
				socklen_t slen = sizeof(val);

				ret = getsockopt(fd, proto, UDP_NO_CHECK6_TX, &val, &slen);
				if (ret == 0) {
					slen = sizeof(val);
					ret = setsockopt(fd, proto, UDP_NO_CHECK6_TX, &val, slen);
				}
				(void)ret;
			}
#else
			UNEXPECTED
#endif
#if defined(UDP_NO_CHECK6_RX)
			{
				int val, ret;
				socklen_t slen = sizeof(val);
				ret = getsockopt(fd, proto, UDP_NO_CHECK6_RX, &val, &slen);
				if (ret == 0) {
					slen = sizeof(val);
					ret = setsockopt(fd, proto, UDP_NO_CHECK6_RX, &val, slen);
				}
				(void)ret;
			}
#else
			UNEXPECTED
#endif
#if defined(UDP_SEGMENT)
			{
				int val, ret;
				socklen_t slen = sizeof(val);

				ret = getsockopt(fd, proto, UDP_SEGMENT, &val, &slen);
				if (ret == 0) {
					slen = sizeof(val);
					ret = setsockopt(fd, proto, UDP_SEGMENT, &val, slen);
				}
				(void)ret;
			}
#else
			UNEXPECTED
#endif
			do {
				size_t i;

				for (i = 16; i < sizeof(buf); i += 16, j++) {
					(void)memset(buf, 'A' + (j % 26), sizeof(buf));
					ssize_t ret = sendto(fd, buf, i, 0, addr, len);
					if (ret < 0) {
						if ((errno == EINTR) || (errno == ENETUNREACH))
							break;
						pr_fail("%s: sendto failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						break;
					}
				}
#if defined(SIOCOUTQ)
				{
					int pending;

					(void)ioctl(fd, SIOCOUTQ, &pending);
				}
#else
			UNEXPECTED
#endif
			} while (keep_stressing(args));
			(void)close(fd);
		} while (keep_stressing(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
		if ((udp_domain == AF_UNIX) && addr) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

			(void)shim_unlink(addr_un->sun_path);
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
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
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
				pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif
		if (bind(fd, addr, addr_len) < 0) {
			pr_fail("%s: bind failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die_close;
		}
#if !defined(__minix__)
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
			/*
			 *  Some systems don't support SO_REUSEADDR
			 */
			if (errno != EINVAL) {
				pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto die_close;
			}
		}
#endif

#if defined(UDP_GRO)
		if (udp_gro) {
			int val, ret;
			socklen_t slen = sizeof(val);

			val = 1;
			ret = setsockopt(fd, proto, UDP_GRO, &val, slen);
			(void)ret;
		}
#endif
		do {
			socklen_t len = addr_len;
			ssize_t n;
#if defined(SIOCOUTQ)
			{
				int pending;

				(void)ioctl(fd, SIOCINQ, &pending);
			}
#else
			UNEXPECTED
#endif

			n = recvfrom(fd, buf, sizeof(buf), 0, addr, &len);
			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_fail("%s: recvfrom failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				break;
			}
			inc_counter(args);
		} while (keep_stressing(args));

die_close:
		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
		(void)close(fd);
die:
		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
		if ((udp_domain == AF_UNIX) && addr) {
			struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

			(void)shim_unlink(addr_un->sun_path);
		}
#endif
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		}
	}
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_udp_domain,	stress_set_udp_domain },
	{ OPT_udp_port,		stress_set_udp_port },
	{ OPT_udp_lite,		stress_set_udp_lite },
	{ OPT_udp_gro,		stress_set_udp_gro },
	{ OPT_udp_if,		stress_set_udp_if },
	{ 0,			NULL }
};

stressor_info_t stress_udp_info = {
	.stressor = stress_udp,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
