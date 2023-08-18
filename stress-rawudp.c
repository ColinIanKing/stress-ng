// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-net.h"

#if defined(HAVE_LINUX_UDP_H)
#include <linux/udp.h>
#endif

#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif

#include <arpa/inet.h>

#define MIN_RAWUDP_PORT		(1024)
#define MAX_RAWUDP_PORT		(65535)
#define DEFAULT_RAWUDP_PORT	(13000)

#if !defined(SOL_UDP)
#define SOL_UDP 	(17)
#endif

static const stress_help_t help[] = {
	{ NULL, "rawudp N",	"start N workers exercising raw UDP socket I/O" },
	{ NULL,	"rawudp-if I",	"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"rawudp-ops N",	"stop after N raw socket UDP bogo operations" },
	{ NULL,	"rawudp-port P","use raw socket ports P to P + number of workers - 1" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_rawudp_supported()
 *      check if we can run this as root
 */
static int stress_rawudp_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_RAW)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_set_rawudp_port()
 *	set port to use
 */
static int stress_set_rawudp_port(const char *opt)
{
	int port;

	stress_set_net_port("rawudp-port", opt,
		MIN_RAWUDP_PORT, MAX_RAWUDP_PORT, &port);
	return stress_set_setting("rawudp-port", TYPE_ID_INT, &port);
}

static int stress_set_rawudp_if(const char *name)
{
	return stress_set_setting("rawudp-if", TYPE_ID_STR, name);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_rawudp_port,	stress_set_rawudp_port },
	{ OPT_rawudp_if,	stress_set_rawudp_if },
	{ 0,			NULL }
};

#if defined(HAVE_LINUX_UDP_H)

#define PACKET_SIZE	sizeof(struct iphdr) + sizeof(struct udphdr) + sizeof(pid_t)

/*
 *  stress_rawudp_client()
 *	client sender
 */
static void NORETURN OPTIMIZE3 stress_rawudp_client(
	const stress_args_t *args,
	in_addr_t addr,
	const int port)
{
	int rc = EXIT_FAILURE;
	uint16_t id = 12345;
	char buf[PACKET_SIZE];
	struct iphdr *ip = (struct iphdr *)buf;
	struct udphdr *udp = (struct udphdr *)(buf + sizeof(struct iphdr));
	uint8_t *data = (uint8_t *)(buf + sizeof(struct iphdr) + sizeof(struct udphdr));
	struct sockaddr_in s_in;
	int one = 1;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	(void)shim_memset(buf, 0, sizeof(buf));

	s_in.sin_family = AF_INET;
	s_in.sin_port = (in_port_t)port;
	s_in.sin_addr.s_addr = addr;

	ip->ihl      = 5;	/* Header length in 32 bit words */
	ip->version  = 4;	/* IPv4 */
	ip->tos      = stress_mwc8() & 0x1e;
	ip->tot_len  = PACKET_SIZE;
	ip->ttl      = 16;  	/* Not too many hops! */
	ip->protocol = SOL_UDP;	/* UDP protocol */
	ip->saddr = (in_addr_t)addr;
	ip->daddr = (in_addr_t)addr;

	udp->source = htons(port);
	udp->dest = htons(port);
	udp->len = htons(sizeof(struct udphdr));

	do {
		int fd;
		ssize_t n;

		if (UNLIKELY((fd = socket(PF_INET, SOCK_RAW, IPPROTO_UDP)) < 0)) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto err;
		}

		if (UNLIKELY(setsockopt(fd,  IPPROTO_IP, IP_HDRINCL, &one, sizeof(one)) < 0)) {
			pr_fail("%s: setsocketopt failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			goto err;
		}

		ip->tos = stress_mwc8() & 0x1e;
		ip->id = htons(id++);
		ip->check = stress_ipv4_checksum((uint16_t *)buf, PACKET_SIZE);

		*(pid_t *)data = args->pid;

		n = sendto(fd, buf, ip->tot_len, 0, (struct sockaddr *)&s_in, sizeof(s_in));
		if (UNLIKELY(n < 0)) {
			pr_fail("%s: raw socket sendto failed on port %d, errno=%d (%s)\n",
				args->name, port, errno, strerror(errno));
		}
		(void)close(fd);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;

err:
	_exit(rc);
}

/*
 *  stress_rawudp_server()
 *	server reader
 */
static int OPTIMIZE3 stress_rawudp_server(
	const stress_args_t *args,
	in_addr_t addr,
	const int port)
{
	int fd;
	socklen_t addr_len;
	int rc = EXIT_SUCCESS;
	struct sockaddr_in s_in;
	char buf[PACKET_SIZE];
	const struct iphdr *ip = (struct iphdr *)buf;
	const struct udphdr *udp = (struct udphdr *)(buf + sizeof(struct iphdr));
	const uint8_t *data = (uint8_t *)(buf + sizeof(struct iphdr) + sizeof(struct udphdr));
	double t_start, duration = 0.0, bytes = 0.0, rate;
	char msg[64];

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_UDP)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}

	s_in.sin_family = AF_INET;
	s_in.sin_port = htons(port);
	s_in.sin_addr.s_addr = addr;
	addr_len = (socklen_t)sizeof(s_in);

	if ((bind(fd, (struct sockaddr *)&s_in, addr_len) < 0)) {
		rc = stress_exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}

	t_start = stress_time_now();
	do {
		ssize_t n;

		n = recv(fd, buf, sizeof(buf), 0);
		if (LIKELY(n > 0)) {
			if ((ip->saddr == addr) &&
			    (ip->protocol == SOL_UDP) &&
			    (ntohs(udp->source) == port)) {
				if (UNLIKELY(*(const pid_t *)data != args->pid)) {
					pr_fail("%s: data check failure, "
						"got 0x%" PRIxMAX ", "
						 "expected 0x%" PRIxMAX "\n",
						args->name,
						(intmax_t)*(const pid_t *)data,
						(intmax_t)args->pid);
					rc = EXIT_FAILURE;
				}
				bytes += (double)n;
				stress_bogo_inc(args);
			}
		}
	} while (stress_continue(args));

	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB recv'd per sec", rate / (double)MB);
	rate = (duration > 0.0) ? (double)stress_bogo_get(args) / duration : 0.0;
	(void)snprintf(msg, sizeof(msg), "packets (%zu bytes) received per sec", PACKET_SIZE);
	stress_metrics_set(args, 1, msg, rate);

die_close:
	(void)close(fd);
die:
	return rc;
}

static void stress_sock_sigpipe_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
}

/*
 *  stress_rawudp
 *	stress raw socket I/O UDP packet send/receive
 */
static int stress_rawudp(const stress_args_t *args)
{
	pid_t pid;
	int rawudp_port = DEFAULT_RAWUDP_PORT;
	int rc = EXIT_FAILURE, reserved_port, parent_cpu;
	in_addr_t addr = (in_addr_t)inet_addr("127.0.0.1");
	char *rawudp_if = NULL;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("rawudp-if", &rawudp_if);
	(void)stress_get_setting("rawudp-port", &rawudp_port);

	if (rawudp_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(rawudp_if, AF_INET, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, rawudp_if, stress_net_domain(AF_INET));
			rawudp_if = NULL;
		} else {
			addr = ((struct sockaddr_in *)&if_addr)->sin_addr.s_addr;
		}
	}

	rawudp_port += args->instance;
	reserved_port = stress_net_reserve_ports(rawudp_port, rawudp_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, rawudp_port);
		return EXIT_NO_RESOURCE;
	}
	rawudp_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, rawudp_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args)) {
			rc = EXIT_SUCCESS;
			goto finish;
		}
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	} else if (pid == 0) {
		(void)stress_change_cpu(args, parent_cpu);
		stress_rawudp_client(args, addr, rawudp_port);
	} else {
		int status;

		rc = stress_rawudp_server(args, addr, rawudp_port);
		(void)shim_kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(rawudp_port, rawudp_port);

	return rc;
}

stressor_info_t stress_rawudp_info = {
	.stressor = stress_rawudp,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_rawudp_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_rawudp_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_rawudp_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/udp.h or only supported on Linux"
};
#endif
