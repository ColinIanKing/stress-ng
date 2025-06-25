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

static int stress_rawsock_open(const char *name, int *fd)
{
	if ((*fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) < 0) {
		*fd = -1;
		if (errno == EPROTONOSUPPORT) {
			pr_inf_skip("%s: stressor will be skipped, protocol not supported\n", name);
			return EXIT_NOT_IMPLEMENTED;
		}
		if ((errno == EPERM) || (errno == EACCES)) {
			pr_inf_skip("%s: stressor will be skipped, permission denied\n", name);
#if defined(__linux__)
			pr_inf("%s: writing 0 0 to /proc/sys/net/ipv4/ping_group_range may help\n", name);
#endif
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: stressor will be skipped, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_ping_sock_supported()
 *	check if we can run this
 */
static int stress_rawsock_supported(const char *name)
{
	int fd;

	if (stress_rawsock_open(name, &fd) != EXIT_SUCCESS)
		return -1;

	(void)close(fd);
	return 0;
}

/*
 *  stress_ping_sock
 *	UDP flood
 */
static int stress_ping_sock(stress_args_t *args)
{
	int fd, rc, j = 0;
	struct sockaddr_in addr;
	struct icmphdr *icmp_hdr;
	int rand_port;
	char ALIGN64 buf[sizeof(*icmp_hdr) + PING_PAYLOAD_SIZE];
	double t, duration = 0.0, rate;

	rc = stress_rawsock_open(args->name, &fd);
	if (rc != EXIT_SUCCESS)
		return rc;

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	icmp_hdr = (struct icmphdr *)buf;
	(void)shim_memset(icmp_hdr, 0, sizeof(*icmp_hdr));
	icmp_hdr->type = ICMP_ECHO;
	icmp_hdr->un.echo.id = (uint16_t)getpid();	/* some unique ID */
	icmp_hdr->un.echo.sequence = 1;

	rand_port = 1024 + stress_mwc16modn(65535 - 1024);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
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
	stress_metrics_set(args, 0, "ping sendto calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	(void)close(fd);

	return rc;
}

const stressor_info_t stress_ping_sock_info = {
	.stressor = stress_ping_sock,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.supported = stress_rawsock_supported,
	.help = help
};
#else
const stressor_info_t stress_ping_sock_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without netinet/ip_icmp.h, SOCK_DGRAM, IPPROTO_ICMP or struct icmphdr"
};
#endif
