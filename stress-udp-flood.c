/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef AF_INET6
#include <netinet/in.h>
#endif
#ifdef AF_UNIX
#include <sys/un.h>
#endif

#include "stress-ng.h"

#if defined(STRESS_UDP_FLOOD)

static int opt_udp_flood_domain = AF_INET;

/*
 *  stress_set_udp_domain()
 *      set the udp domain option
 */
int stress_set_udp_flood_domain(const char *name)
{
	return stress_set_net_domain(DOMAIN_INET_ALL, "udp-flood-domain", name, &opt_udp_flood_domain);
}

/*
 *  stress_udp_flood
 *	UDP flood
 */
int stress_udp_flood(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	pid_t pid = getpid();
	int port = 1024;
	struct sockaddr *addr;
	socklen_t addr_len;
	const size_t sz_max = 23 + instance;
	size_t sz = 1;

	static const char data[64] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUV"
		"WXYZabcdefghijklmnopqrstuvwxyz@!";

	if ((fd = socket(opt_udp_flood_domain, SOCK_DGRAM, AF_PACKET)) < 0) {
		pr_fail_dbg(name, "socket");
		return EXIT_FAILURE;
	}
	stress_set_sockaddr(name, instance, pid,
		opt_udp_flood_domain, port, &addr, &addr_len);

	do {
		char buf[sz];

		stress_set_sockaddr_port(opt_udp_flood_domain, port, addr);

		memset(buf, data[j++ & 63], sz);
		if (sendto(fd, buf, sz, 0, addr, addr_len) > 0)
			(*counter)++;
		if (++port > 65535)
			port = 1024;
		if (++sz > sz_max)
			sz = 1;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);

	return rc;
}

#endif
