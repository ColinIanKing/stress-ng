/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__)

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

#define MAX_PAYLOAD_SIZE	(1000)

/*
 *  stress_icmp_flood_supported()
 *      check if we can run this as root
 */
int stress_icmp_flood_supported(void)
{
	if (geteuid() != 0) {
		pr_inf("icmp flood stressor will be skipped, "
			"need to be running as root for this stressor\n");
		return -1;
	}
	return 0;
}

static inline uint32_t checksum(uint16_t *ptr, size_t n)
{
	uint32_t sum = 0;

	while (n > 1) {
		sum += *ptr++;
		n -= 2;
	}

	if (n)
		sum += *(uint8_t*)ptr;

	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return ~sum;
}

/*
 *  stress_icmp_flood
 *	stress local host with ICMP flood
 */
int stress_icmp_flood(const args_t *args)
{
	int fd, rc = EXIT_FAILURE;
	const int set_on = 1;
	const unsigned long addr = inet_addr("127.0.0.1");
	struct sockaddr_in servaddr;
	uint64_t sendto_fails = 0;

	fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW);
	if (fd < 0) {
		pr_fail_err("socket");
		goto err;
	}
	if (setsockopt(fd, IPPROTO_IP, IP_HDRINCL,
		(const char *)&set_on, sizeof(set_on)) < 0) {
		pr_fail_err("setsockopt IP_HDRINCL");
		goto err_socket;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST,
		(const char *)&set_on, sizeof(set_on)) < 0) {
		pr_fail_err("setsockopt SO_BROADCAST");
		goto err_socket;
	}

	servaddr.sin_family = AF_INET;
	servaddr.sin_addr.s_addr = addr;
	memset(&servaddr.sin_zero, 0, sizeof(servaddr.sin_zero));

	do {
		const size_t payload_len = (mwc32() % MAX_PAYLOAD_SIZE) + 1;
		const size_t pkt_len =
			sizeof(struct iphdr) + sizeof(struct icmphdr) + payload_len;
		char pkt[pkt_len];
		struct iphdr *ip_hdr = (struct iphdr *)pkt;
		struct icmphdr *icmp_hdr = (struct icmphdr *)(pkt + sizeof(struct iphdr));

		memset(pkt, 0, sizeof(pkt));

		ip_hdr->version = 4;
		ip_hdr->ihl = 5;
		ip_hdr->tos = 0;
		ip_hdr->tot_len = htons(pkt_len);
		ip_hdr->id = mwc32();
		ip_hdr->frag_off = 0;
		ip_hdr->ttl = 64;
		ip_hdr->protocol = IPPROTO_ICMP;
		ip_hdr->saddr = addr;
		ip_hdr->daddr = addr;

		icmp_hdr->type = ICMP_ECHO;
		icmp_hdr->code = 0;
		icmp_hdr->un.echo.sequence = mwc32();
		icmp_hdr->un.echo.id = mwc32();

		/*
		 * Generating random data is expensive so do it every 64 packets
		 */
		if ((*args->counter & 0x3f) == 0)
			stress_strnrnd(pkt + sizeof(struct iphdr) +
				sizeof(struct icmphdr), payload_len);
		icmp_hdr->checksum = checksum((uint16_t *)icmp_hdr,
			sizeof(struct icmphdr) + payload_len);

		if ((sendto(fd, pkt, pkt_len, 0,
			   (struct sockaddr*)&servaddr, sizeof(servaddr))) < 1) {
			sendto_fails++;
		}
		inc_counter(args);
	} while (keep_stressing());

	pr_dbg("%s: %.2f%% of %" PRIu64 " sendto messages succeeded.\n",
		args->name,
		100.0 * (float)(*args->counter - sendto_fails) / *args->counter,
		*args->counter);

	rc = EXIT_SUCCESS;

err_socket:
	(void)close(fd);
err:
	return rc;
}
#else

int stress_icmp_flood_supported(void)
{
	pr_inf("icmp flood stressor will be skipped, not supported on this machine\n");
	return -1;
}

int stress_icmp_flood(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
