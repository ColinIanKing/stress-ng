/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

typedef struct {
	const char *name;
	const int  domain;
	const int  domain_flags;
} domain_t;

static const domain_t domains[] = {
	{ "ipv4",	AF_INET,	DOMAIN_INET },
	{ "ipv6",	AF_INET6,	DOMAIN_INET6 },
	{ "unix",	AF_UNIX,	DOMAIN_UNIX },
};

/*
 *  stress_set_net_port()
 *	set up port number from opt
 */
void stress_set_net_port(
	const char *optname,
	const char *opt,
	const int min_port,
	const int max_port,
	int *port)
{
	*port = get_uint64(opt);
	check_range(optname, *port,
		min_port, max_port - STRESS_PROCS_MAX);
}

/*
 *  stress_set_net_domain()
 *	set the domain option
 */
int stress_set_net_domain(
	const int domain_mask,
	const char *name,
	const char *domain_name,
	int *domain)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(domains); i++) {
		if ((domain_mask & domains[i].domain_flags) &&
		    !strcmp(domain_name, domains[i].name)) {
			*domain = domains[i].domain;
			return 0;
		}
	}
	(void)fprintf(stderr, "%s: domain must be one of:", name);
	for (i = 0; i < SIZEOF_ARRAY(domains); i++)
		if (domain_mask & domains[i].domain_flags)
			(void)fprintf(stderr, " %s", domains[i].name);
	(void)fprintf(stderr, "\n");
	*domain = 0;
	return -1;
}

/*
 *  setup socket address
 */
void stress_set_sockaddr(
	const char *name,
	const uint32_t instance,
	const pid_t ppid,
	const int domain,
	const int port,
	struct sockaddr **sockaddr,
	socklen_t *len,
	const int net_addr)
{
	switch (domain) {
#if defined(AF_INET)
	case AF_INET: {
		static struct sockaddr_in addr;

		(void)memset(&addr, 0, sizeof(addr));
		addr.sin_family = domain;
		switch (net_addr) {
		case NET_ADDR_LOOPBACK:
			addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
			break;
		case NET_ADDR_ANY:
		default:
			addr.sin_addr.s_addr = htonl(INADDR_ANY);
			break;
		}
		addr.sin_port = htons(port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#if defined(AF_INET6)
	case AF_INET6: {
		static struct sockaddr_in6 addr;
#if defined(__minix__)
		static const struct in6_addr in6addr_any = IN6ADDR_ANY_INIT;
		static const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
#endif
		(void)memset(&addr, 0, sizeof(addr));
		addr.sin6_family = domain;
		switch (net_addr) {
		case NET_ADDR_LOOPBACK:
			addr.sin6_addr = in6addr_loopback;
			break;
		case NET_ADDR_ANY:
		default:
			addr.sin6_addr = in6addr_any;
			break;
		}
		addr.sin6_port = htons(port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#if defined(AF_UNIX)
	case AF_UNIX: {
		static struct sockaddr_un addr;

		(void)memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		(void)snprintf(addr.sun_path, sizeof(addr.sun_path),
			"/tmp/stress-ng-%d-%" PRIu32,
			(int)ppid, instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
	default:
		pr_fail("%s: unknown domain %d\n", name, domain);
		(void)kill(getppid(), SIGALRM);
		_exit(EXIT_FAILURE);
	}
}

/*
 *  setup just the socket address port
 */
void HOT stress_set_sockaddr_port(
	const int domain,
	const int port,
	struct sockaddr *sockaddr)
{
	switch (domain) {
#if defined(AF_INET)
	case AF_INET: {
		struct sockaddr_in *addr = (struct sockaddr_in *)sockaddr;

		addr->sin_port = htons(port);
		break;
	}
#endif
#if defined(AF_INET6)
	case AF_INET6: {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)sockaddr;
		addr->sin6_port = htons(port);
		break;
	}
#endif
#if defined(AF_UNIX)
	case AF_UNIX:
		break;
#endif
	default:
		break;
	}
}
