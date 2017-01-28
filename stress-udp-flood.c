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

#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(AF_INET6)
#include <netinet/in.h>
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif

static int opt_udp_flood_domain = AF_INET;

/*
 *  stress_set_udp_domain()
 *      set the udp domain option
 */
int stress_set_udp_flood_domain(const char *name)
{
	return stress_set_net_domain(DOMAIN_INET_ALL, "udp-flood-domain", name, &opt_udp_flood_domain);
}

#if defined(AF_PACKET)

/*
 *  stress_udp_flood
 *	UDP flood
 */
int stress_udp_flood(const args_t *args)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	int port = 1024;
	struct sockaddr *addr;
	socklen_t addr_len;
	const size_t sz_max = 23 + args->instance;
	size_t sz = 1;

	static const char data[64] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUV"
		"WXYZabcdefghijklmnopqrstuvwxyz@!";

	if ((fd = socket(opt_udp_flood_domain, SOCK_DGRAM, AF_PACKET)) < 0) {
		pr_fail_dbg("socket");
		return EXIT_FAILURE;
	}
	stress_set_sockaddr(args->name, args->instance, args->pid,
		opt_udp_flood_domain, port,
		&addr, &addr_len, NET_ADDR_ANY);

	do {
		char buf[sz];

		stress_set_sockaddr_port(opt_udp_flood_domain, port, addr);

		memset(buf, data[j++ & 63], sz);
		if (sendto(fd, buf, sz, 0, addr, addr_len) > 0)
			inc_counter(args);
		if (++port > 65535)
			port = 1024;
		if (++sz > sz_max)
			sz = 1;
	} while (keep_stressing());

	(void)close(fd);

	return rc;
}
#else
int stress_udp_flood(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
