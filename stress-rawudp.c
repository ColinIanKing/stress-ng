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

#if !defined(SOL_UDP)
#define SOL_UDP 	(17)
#endif
#define PACKET_SIZE	(2048)

static const stress_help_t help[] = {
	{ NULL, "rawudp N",	"start N workers exercising raw UDP socket I/O" },
	{ NULL,	"rawudp-ops N",	"stop after N raw socket UDP bogo operations" },
	{ NULL,	"rawudp-port P","use raw socket ports P to P + number of workers - 1" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_rawudp_supported()
 *      check if we can run this as root
 */
static int stress_rawudp_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_RAW)) {
		pr_inf("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_set_rawudp-port()
 *	set port to use
 */
static int stress_set_port(const char *opt)
{
	int port;

	stress_set_net_port("rawudp-port", opt,
		MIN_RAWUDP_PORT, MAX_RAWUDP_PORT - STRESS_PROCS_MAX,
		&port);
	return stress_set_setting("rawudp-port", TYPE_ID_INT, &port);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_rawudp_port,	stress_set_port },
	{ 0,			NULL }
};


#if defined(HAVE_LINUX_UDP_H)

/*
 *  stress_rawudp_client()
 *	client sender
 */
static void stress_rawudp_client(
	const stress_args_t *args,
	const pid_t ppid,
	unsigned long addr,
	const int port)
{
	int rc = EXIT_FAILURE;
	uint16_t id = 12345;
	char buf[PACKET_SIZE];
	struct iphdr *ip = (struct iphdr *)buf;
	struct udphdr *udp = (struct udphdr *)(buf + sizeof(struct iphdr));
	struct sockaddr_in sin;
	int one = 1;

	(void)setpgid(0, g_pgrp);
	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	(void)memset(buf, 0, sizeof(buf));

	sin.sin_family = AF_INET;
	sin.sin_port = port;
	sin.sin_addr.s_addr = addr;

	ip->ihl      = 5;	/* Header length in 32 bit words */
	ip->version  = 4;	/* IPv4 */
	ip->tos      = stress_mwc8() & 0x1e;
	ip->tot_len  = sizeof(struct iphdr) + sizeof(struct udphdr);
	ip->ttl      = 16;  	/* Not too many hops! */
	ip->protocol = SOL_UDP;	/* UDP protocol */
	ip->saddr = addr;
	ip->daddr = addr;

	udp->source = htons(port);
	udp->dest = htons(port);
	udp->len = htons(sizeof(struct udphdr));

	do {
		int fd;
		ssize_t n;

		if ((fd = socket(PF_INET, SOCK_RAW, IPPROTO_UDP)) < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto err;
		}

		if (setsockopt(fd,  IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0) {
			pr_fail("%s: setsocketopt failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		ip->tos = stress_mwc8() & 0x1e;
		ip->id = htons(id++);
		ip->check = stress_ip_checksum((uint16_t *)buf, sizeof(struct iphdr) + sizeof(struct udphdr));

		n = sendto(fd, buf, ip->tot_len, 0, (struct sockaddr *)&sin, sizeof(sin));
		if (n < 0) {
			pr_err("%s: raw socket sendto failed on port %d, errno=%d (%s)\n",
				args->name, port, errno, strerror(errno));
		}
		(void)close(fd);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;

err:
	/* Inform parent we're all done */
	(void)kill(ppid, SIGALRM);
	_exit(rc);
}

/*
 *  stress_rawudp_server()
 *	server reader
 */
static int stress_rawudp_server(
	const stress_args_t *args,
	unsigned long addr,
	const int port)
{
	int fd;
	socklen_t addr_len;
	int rc = EXIT_SUCCESS;
	struct sockaddr_in sin;
	char buf[PACKET_SIZE];
	const struct iphdr *ip = (struct iphdr *)buf;
	const struct udphdr *udp = (struct udphdr *)(buf + sizeof(struct iphdr));

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}

	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = addr;
	addr_len = (socklen_t)sizeof(sin);

	if ((bind(fd, (struct sockaddr *)&sin, addr_len) < 0)) {
		rc = exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}

	do {
		ssize_t n;

		n = recv(fd, buf, sizeof(buf), 0);
		if (n > 0) {
			if ((ip->saddr == addr) &&
			    (ip->protocol == SOL_UDP) &&
			    (ntohs(udp->source) == port)) {
				inc_counter(args);
			}
		}
	} while (keep_stressing());

die_close:
	(void)close(fd);
die:
	return rc;
}

static void stress_sock_sigpipe_handler(int signum)
{
	(void)signum;

	keep_stressing_set_flag(false);
}

/*
 *  stress_rawudp
 *	stress raw socket I/O UDP packet send/receive
 */
static int stress_rawudp(const stress_args_t *args)
{
	pid_t pid;
	int port = DEFAULT_RAWUDP_PORT;
	int rc = EXIT_FAILURE;
	unsigned long addr = inet_addr("127.0.0.1");

	(void)stress_get_setting("rawudp-port", &port);

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, port + args->instance);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	} else if (pid == 0) {
		stress_rawudp_client(args, args->pid, addr, port);
		_exit(EXIT_SUCCESS);
	} else {
		int status;

		rc = stress_rawudp_server(args, addr, port);
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
	return rc;
}

stressor_info_t stress_rawudp_info = {
	.stressor = stress_rawudp,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_rawudp_supported,
	.help = help
};
#else
stressor_info_t stress_rawudp_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_rawudp_supported,
	.help = help
};
#endif
