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

typedef struct {
	const char *name;
	const int  domain;
	const int  domain_flags;
} domain_t;

static const domain_t domains[] = {
	{ "ipv4",	AF_INET,	DOMAIN_INET },
	{ "ipv6",	AF_INET6,	DOMAIN_INET6 },
	{ "unix",	AF_UNIX,	DOMAIN_UNIX },
	{ NULL,		-1, ~0 }
};

/*
 *  stress_set_net_port()
 *	set up port number from opt
 */
void stress_set_net_port(
	const char *optname,
	const char *optarg,
	const int min_port,
	const int max_port,
	int *port)
{
	*port = get_uint64(optarg);
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

	for (i = 0; domains[i].name; i++) {
		if ((domain_mask & domains[i].domain_flags) &&
		    !strcmp(domain_name, domains[i].name)) {
			*domain = domains[i].domain;
			return 0;
		}
	}
	fprintf(stderr, "%s: domain must be one of:", name);
	for (i = 0; domains[i].name; i++)
		if (domain_mask & domains[i].domain_flags)
			fprintf(stderr, " %s", domains[i].name);
	fprintf(stderr, "\n");
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
	socklen_t *len)
{
	switch (domain) {
#ifdef AF_INET
	case AF_INET: {
		static struct sockaddr_in addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin_family = domain;
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
		addr.sin_port = htons(port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#ifdef AF_INET6
	case AF_INET6: {
		static struct sockaddr_in6 addr;

		memset(&addr, 0, sizeof(addr));
		addr.sin6_family = domain;
		addr.sin6_addr = in6addr_any;
		addr.sin6_port = htons(port + instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#ifdef AF_UNIX
	case AF_UNIX: {
		static struct sockaddr_un addr;

		memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		snprintf(addr.sun_path, sizeof(addr.sun_path),
			"/tmp/stress-ng-%d-%" PRIu32,
			ppid, instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
	default:
		pr_fail_dbg(name, "unknown domain");
		(void)kill(getppid(), SIGALRM);
		exit(EXIT_FAILURE);
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
#ifdef AF_INET
	case AF_INET: {
		struct sockaddr_in *addr = (struct sockaddr_in *)sockaddr;

		addr->sin_port = htons(port);
		break;
	}
#endif
#ifdef AF_INET6
	case AF_INET6: {
		struct sockaddr_in6 *addr = (struct sockaddr_in6 *)sockaddr;
		addr->sin6_port = htons(port);
		break;
	}
#endif
#ifdef AF_UNIX
	case AF_UNIX:
		break;
#endif
	default:
		break;
	}
}
