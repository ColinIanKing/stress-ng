// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_NETINET_IP_ICMP_H)
#include <netinet/ip_icmp.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"ping-sock N",		"start N workers that exercises a ping socket" },
	{ NULL,	"ping-sock-ops N",	"stop after N ping sendto messages" },
	{ NULL,	NULL,			NULL }
};

#if defined(PF_INET) &&		\
    defined(SOCK_DGRAM) &&	\
    defined(IPPROTO_ICMP) &&	\
    defined(HAVE_ICMPHDR) &&	\
    defined(__linux__)

#define PING_PAYLOAD_SIZE	(4)

/*
 *  stress_ping_sock
 *	UDP flood
 */
static int stress_ping_sock(const stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	struct sockaddr_in addr;
	struct icmphdr *icmp_hdr;
	int rand_port;
	char ALIGN64 buf[sizeof(*icmp_hdr) + PING_PAYLOAD_SIZE];
	double t, duration = 0.0, rate;

	if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_inf_skip("%s: skipping stressor, protocol not supported\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		if ((errno == EPERM) || (errno == EACCES)) {
			pr_inf_skip("%s: skipping stressor, permission denied\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	icmp_hdr = (struct icmphdr *)buf;
	(void)shim_memset(icmp_hdr, 0, sizeof(*icmp_hdr));
	icmp_hdr->type = ICMP_ECHO;
	icmp_hdr->un.echo.id = (uint16_t)getpid();	/* some unique ID */
	icmp_hdr->un.echo.sequence = 1;

	rand_port = 1024 + stress_mwc16modn(65535 - 1024);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		(void)shim_memset(buf + sizeof(*icmp_hdr), stress_ascii64[j++ & 63], PING_PAYLOAD_SIZE);
		addr.sin_port = htons(rand_port);

		if (LIKELY(sendto(fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, sizeof(addr)) > 0))
			stress_bogo_inc(args);

		icmp_hdr->un.echo.sequence++;
		rand_port++;
		rand_port &= 0xffff;
	} while (stress_continue(args));
	duration = stress_time_now() - t;

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? (double)stress_bogo_get(args) / duration : 0.0;
	stress_metrics_set(args, 0, "ping sendto calls per sec", rate);

	(void)close(fd);

	return rc;
}

stressor_info_t stress_ping_sock_info = {
	.stressor = stress_ping_sock,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_ping_sock_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without netinet/ip_icmp.h, SOCK_DGRAM, IPPROTO_ICMP or struct icmphdr"
};
#endif
