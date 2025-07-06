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
#include "core-capabilities.h"
#include "core-net.h"

#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif

#if defined(HAVE_NETINET_IP_ICMP_H)
#include <netinet/ip_icmp.h>
#endif

#include <arpa/inet.h>

static const stress_help_t help[] = {
	{ NULL,	"icmp-flood N",		"start N ICMP packet flood workers" },
	{ NULL,	"icmp-flood-ops N",	"stop after N ICMP bogo operations (ICMP packets)" },
	{ NULL, "icmp-flood-max-size",	"use randomized packet sizes up to 65535 bytes" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_icmp_flood_max_size, "icmp-flood-max-size", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};


#if defined(HAVE_NETINET_IP_H) &&	\
    defined(HAVE_NETINET_IP_ICMP_H) &&	\
    defined(HAVE_ICMPHDR)

#define MAX_PAYLOAD_SIZE	(65535 - sizeof(struct iphdr) - sizeof(struct icmphdr))
#define DEFAULT_PAYLOAD_SIZE	(1000)
#define MAX_PKT_LEN		(sizeof(struct iphdr) + \
				 sizeof(struct icmphdr) + \
				 MAX_PAYLOAD_SIZE + 1)
/*
 *  stress_icmp_flood_supported()
 *      check if we can run this as root
 */
static int stress_icmp_flood_supported(const char *name)
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
 *  stress_icmp_flood
 *	stress local host with ICMP flood
 */
static int stress_icmp_flood(stress_args_t *args)
{
	int fd, rc = EXIT_FAILURE;
	const int set_on = 1;
	const unsigned long int addr = inet_addr("127.0.0.1");
	struct sockaddr_in servaddr;
	uint64_t counter, sendto_fails = 0, sendto_ok;
	double bytes = 0.0, t_start, duration, rate;
	const uint16_t id = htons((uint16_t)args->instance);
	uint16_t seq = 0;

	char ALIGN64 pkt[MAX_PKT_LEN];
	struct iphdr *const ip_hdr = (struct iphdr *)pkt;
	struct icmphdr *const icmp_hdr = (struct icmphdr *)(pkt + sizeof(struct iphdr));
	char *const payload = pkt + sizeof(struct iphdr) + sizeof(struct icmphdr);
	uint32_t max_payload_size;
	bool icmp_flood_max_size = false;

	if (!stress_get_setting("icmp-flood-max-size", &icmp_flood_max_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			icmp_flood_max_size = 0;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			icmp_flood_max_size = 1;
	}
	max_payload_size = icmp_flood_max_size ? MAX_PAYLOAD_SIZE : DEFAULT_PAYLOAD_SIZE;

	(void)shim_memset(pkt, 0, sizeof(pkt));
	stress_rndbuf(payload, MAX_PAYLOAD_SIZE);

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (fd < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}
	if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL,
		(const char *)&set_on, sizeof(set_on)) < 0) {
		pr_fail("%s: setsockopt IP_HDRINCL  failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err_socket;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
		(const char *)&set_on, sizeof(set_on)) < 0) {
		pr_fail("%s: setsockopt SO_BROADCAST failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err_socket;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = (in_addr_t)addr;
	(void)shim_memset(&servaddr.sin_zero, 0, sizeof(servaddr.sin_zero));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(pkt, 0, sizeof(pkt));
	ip_hdr->version = 4;
	ip_hdr->ihl = 5;
	ip_hdr->tos = 0;
	ip_hdr->id = id;
	ip_hdr->frag_off = 0;
	ip_hdr->ttl = 64;
	ip_hdr->protocol = IPPROTO_ICMP;
	ip_hdr->saddr = (in_addr_t)addr;
	ip_hdr->daddr = (in_addr_t)addr;

	icmp_hdr->type = ICMP_ECHO;
	icmp_hdr->code = 0;
	icmp_hdr->un.echo.sequence = htons(seq);

	stress_rndbuf(payload, MAX_PAYLOAD_SIZE);

	t_start = stress_time_now();
	do {
		const size_t payload_len = stress_mwc32modn(max_payload_size) + 1;
		const size_t pkt_len =
			sizeof(struct iphdr) + sizeof(struct icmphdr) + payload_len;
		ssize_t ret;

		ip_hdr->tot_len = htons(pkt_len);
		icmp_hdr->un.echo.id = id;

		payload[0]++;
		icmp_hdr->checksum = stress_ipv4_checksum((uint16_t *)icmp_hdr,
			sizeof(struct icmphdr) + payload_len);

		ret = sendto(fd, pkt, pkt_len, 0, (struct sockaddr*)&servaddr, sizeof(servaddr));
		if (UNLIKELY(ret < 0)) {
			sendto_fails++;
		} else {
			bytes += (double)ret;
		}
		stress_bogo_inc(args);
		seq++;
	} while (stress_continue(args));
	duration = stress_time_now() - t_start;

	counter = stress_bogo_get(args);
	sendto_ok = counter - sendto_fails;

	rate = (duration > 0.0) ? sendto_ok / duration : 0.0;
	stress_metrics_set(args, 0, "sendto calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 1, "MB written per sec",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);
	rate = (counter > 0) ? 100.0 * (double)sendto_ok / (double)counter : 0.0;
	stress_metrics_set(args, 2, "% successful sendto messages",
		rate / args->instances, STRESS_METRIC_TOTAL);

	rc = EXIT_SUCCESS;

err_socket:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	return rc;
}

const stressor_info_t stress_icmp_flood_info = {
	.stressor = stress_icmp_flood,
	.supported = stress_icmp_flood_supported,
	.classifier = CLASS_OS | CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_icmp_flood_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS | CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without netinet/ip.h, netinet/ip_icmp.h or struct icmphdr support"
};
#endif
