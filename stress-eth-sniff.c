/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-capabilities.h"
#include "core-net.h"
#include "core-mmap.h"
#include "core-shim.h"

#include <sys/ioctl.h>

#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif

#if defined(HAVE_NET_ETHERNET_H)
#include <net/ethernet.h>
#endif

static const stress_help_t help[] = {
	{ NULL, "eth_sniff N",		"start N workers sniffing ethernet packets" },
	{ NULL,	"eth_sniff-ops N",	"stop after N bogo sniffing ethernet packet operations" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_eth_sniff_supported()
 *      check if we can run this as root
 */
static int stress_eth_sniff_supported(const char *name)
{
	if (!stress_capabilities_check(SHIM_CAP_NET_RAW)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

#if defined(HAVE_NET_ETHERNET_H) &&	\
    defined(HAVE_NETINET_IP_H)	&&	\
    defined(AF_PACKET) &&		\
    defined(SOCK_RAW) &&		\
    defined(ETH_P_ALL) &&		\
    defined(__linux__)

#define PROTO_MAX	(256)

typedef struct common_proto {
	uint8_t proto;		/* protocol number */
	char *name;		/* name of protocol */
} common_proto_t;

/* commonly used protocols */
static const common_proto_t common_proto[] = {
	{ 1,	"ICMP" },
	{ 2,	"IGMP" },
	{ 6,	"TCP" },
	{ 17,	"UDP" },
	{ 27,	"RDP" },
	{ 33,	"DCCP" },
	{ 132,	"SCTP" },
	{ 136,  "UDPLite" },
};

static void stress_eth_sniff_metrics(
	stress_args_t *args,
	const char *domain,
	const double ip_proto[PROTO_MAX])
{
	size_t i;
	char str[64];

	for (i = 0; i < SIZEOF_ARRAY(common_proto); i++) {
		const uint8_t proto = common_proto[i].proto;
		const double rate = ip_proto[proto];

		if (rate > 0.0) {
			(void)snprintf(str, sizeof(str), "%s %s KB per sec", domain, common_proto[i].name);
				stress_metrics_set(args, str, rate / 1024.0, STRESS_METRIC_GEOMETRIC_MEAN);
		}
	}
}

/*
 *  stress_eth_sniff
 *	stress raw socket I/O UDP packet send/receive
 */
static int stress_eth_sniff(stress_args_t *args)
{
	int fd;
	const size_t buf_size = 65534;
	uint8_t *buf;
	double ipv4_proto[PROTO_MAX];
	double ipv6_proto[PROTO_MAX];
	size_t i;
	double t, duration;

	for (i = 0; i < PROTO_MAX; i++) {
		ipv4_proto[i] = 0.0;
		ipv6_proto[i] = 0.0;
	}

	buf = stress_mmap_populate(NULL, buf_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: mmap of %zu bytes failed, skipping stressor\n",
			args->name, buf_size);
		return EXIT_NO_RESOURCE;
	}

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
	if (fd < 0) {
		pr_inf("%s: socket failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}

	t = stress_time_now();
	do {
		ssize_t len;
		uint8_t type;

		len = recvfrom(fd, buf, buf_size, 0, NULL, NULL);
		if (len < (14 + 20))
			continue;

		type = buf[14] >> 4;

		switch (type) {
		case 4:		/* IP4 */
			if ((buf[14] & 0xf) < 5)
				continue;
			ipv4_proto[buf[14 + 9]] += (double)(len - 14);
			stress_bogo_inc(args);
			break;
		case 6:		/* IP6 */
			ipv6_proto[buf[14 + 6]] += (double)(len - 14);
			stress_bogo_inc(args);
			break;
		}
	} while (stress_continue(args));
	duration = stress_time_now() - t;

	(void)close(fd);

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (duration > 0.0) {
		stress_eth_sniff_metrics(args, "IPv4", ipv4_proto);
		stress_eth_sniff_metrics(args, "IPv6", ipv6_proto);
	}
	return EXIT_SUCCESS;
}

const stressor_info_t stress_eth_sniff_info = {
	.stressor = stress_eth_sniff,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.supported = stress_eth_sniff_supported,
	.verify = VERIFY_NONE,
	.help = help
};
#else
const stressor_info_t stress_eth_sniff_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.supported = stress_eth_sniff_supported,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without one or more of net/ethernet.h, netinet/ip.h, AF_PACKET, SOCK_RAW, ETH_P_ALL or Linux"
};
#endif
