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
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-lock.h"
#include "core-net.h"

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#include <netinet/in.h>

#if defined(HAVE_IFADDRS_H)
#include <ifaddrs.h>
#endif
#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif

typedef struct {
	const char *name;
	const int  domain;
	const int  domain_flags;
} stress_domain_t;

static const stress_domain_t domains[] = {
	{ "ipv4",	AF_INET,	DOMAIN_INET },
#if defined(AF_INET6)
	{ "ipv6",	AF_INET6,	DOMAIN_INET6 },
#endif
	{ "unix",	AF_UNIX,	DOMAIN_UNIX },
};

/*
 *  stress_net_interface_exists()
 *	check if interface exists, returns -1 if failed / not found
 *	0 if interface exists
 */
int stress_net_interface_exists(const char *interface, const int domain, struct sockaddr *addr)
{
#if defined(HAVE_IFADDRS_H)
	struct ifaddrs *ifaddr, *ifa;
	int ret = -1;

	if (UNLIKELY(!interface))
		return -1;
	if (UNLIKELY(!addr))
		return -1;

	if (UNLIKELY(getifaddrs(&ifaddr) < 0))
		return -1;
	for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
		if (!ifa->ifa_addr)
			continue;
		if (!ifa->ifa_name)
			continue;
		if (ifa->ifa_addr->sa_family != domain)
			continue;
		if (strcmp(ifa->ifa_name, interface) == 0) {
			(void)shim_memcpy(addr, ifa->ifa_addr, sizeof(*addr));
			ret = 0;
			break;
		}
	}
	freeifaddrs(ifaddr);

	return ret;
#else
	(void)interface;
	(void)domain;
	(void)addr;

	return -1;
#endif
}

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
	const uint64_t val = stress_get_uint64(opt);

	stress_check_range(optname, val, (uint64_t)min_port, (uint64_t)max_port);
	*port = (int)val;
}

/*
 *  stress_net_domain()
 *	return human readable domain name from domain number
 */
const char CONST *stress_net_domain(const int domain)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(domains); i++) {
		if (domains[i].domain == domain)
			return domains[i].name;
	}
	return "unknown";
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
 *  stress_set_sockaddr_if()
 * 	setup socket address with optional interface name
 */
int stress_set_sockaddr_if(
	const char *name,
	const uint32_t instance,
	const pid_t pid,
	const int domain,
	const int port,
	const char *ifname,
	struct sockaddr **sockaddr,
	socklen_t *len,
	const int net_addr)
{
	uint16_t sin_port = (uint16_t)port;

	(void)instance;
	(void)pid;

	*sockaddr = NULL;
	*len = 0;

	/* omit ports 0..1023 */
	if (UNLIKELY(sin_port < 1024))
		sin_port += 1024;

	switch (domain) {
#if defined(AF_INET)
	case AF_INET: {
		static struct sockaddr_in addr;

		(void)shim_memset(&addr, 0, sizeof(addr));

		if ((!ifname) || (!stress_net_interface_exists(ifname, domain, (struct sockaddr *)&addr))) {
			switch (net_addr) {
			case NET_ADDR_LOOPBACK:
				addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
				break;
			case NET_ADDR_ANY:
			default:
				addr.sin_addr.s_addr = htonl(INADDR_ANY);
				break;
			}
		}
		addr.sin_family = (sa_family_t)domain;
		addr.sin_port = htons(sin_port);
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
		(void)shim_memset(&addr, 0, sizeof(addr));

		if ((!ifname) || (!stress_net_interface_exists(ifname, domain, (struct sockaddr *)&addr))) {
			switch (net_addr) {
			case NET_ADDR_LOOPBACK:
				addr.sin6_addr = in6addr_loopback;
				break;
			case NET_ADDR_ANY:
			default:
				addr.sin6_addr = in6addr_any;
				break;
			}
		}
		addr.sin6_family = (sa_family_t)domain;
		addr.sin6_port = htons(sin_port);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	case AF_UNIX: {
		static struct sockaddr_un addr;

		(void)shim_memset(&addr, 0, sizeof(addr));
		addr.sun_family = AF_UNIX;
		(void)snprintf(addr.sun_path, sizeof(addr.sun_path),
			"/tmp/stress-ng-%" PRIdMAX "-%" PRIu32,
			(intmax_t)pid, instance);
		*sockaddr = (struct sockaddr *)&addr;
		*len = sizeof(addr);
		break;
	}
#endif
	default:
		pr_fail("%s: unknown domain %d\n", name, domain);
		return -1;
	}
	return 0;
}

/*
 *  stress_set_sockaddr()
 * 	setup socket address without interface name
 */
int stress_set_sockaddr(
	const char *name,
	const uint32_t instance,
	const pid_t pid,
	const int domain,
	const int port,
	struct sockaddr **sockaddr,
	socklen_t *len,
	const int net_addr)
{
	return stress_set_sockaddr_if(name, instance, pid, domain, port, NULL, sockaddr, len, net_addr);
}

/*
 *  stress_set_sockaddr_port()
 *	setup just the socket address port
 */
void stress_set_sockaddr_port(
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

/*
 *  stress_net_port_range_ok()
 *	port range sanity check, returns true if OK
 */
static bool CONST stress_net_port_range_ok(const int start_port, const int end_port)
{
	if (UNLIKELY((start_port > end_port)))
		return false;
	if (UNLIKELY((start_port < 0) || (start_port >= 65536)))
		return false;
	if (UNLIKELY((end_port < 0) || (end_port >= 65536)))
		return false;
	return true;
}

/*
 *   stress_net_reserve_ports()
 *	attempt to reserve ports, returns nearest available contiguous
 *	ports that are available or -1 if none could be found
 */
int stress_net_reserve_ports(const int start_port, const int end_port)
{
	int i, port = -1;

	if (UNLIKELY(!stress_net_port_range_ok(start_port, end_port)))
		return -1;

	if (UNLIKELY(stress_lock_acquire(g_shared->net_port_map.lock) < 0))
		return -1;

	if (LIKELY(start_port == end_port)) {
		/* most cases just request one port */
		for (i = start_port; i < 65536; i++) {
			if (STRESS_GETBIT(g_shared->net_port_map.allocated, i) == 0) {
				STRESS_SETBIT(g_shared->net_port_map.allocated, i);
				port = i;
				break;
			}
		}
	} else {
		const int quantity = (end_port - start_port) + 1;
		int j = 0;

		/* otherwise scan for contiguous port range */
		for (i = start_port; i < 65536; i++) {
			if (STRESS_GETBIT(g_shared->net_port_map.allocated, i) == 0) {
				j++;
				if (j == quantity) {
					port = i + 1 - quantity;
					break;
				}
			} else {
				port = -1;
				j = 0;
			}
		}
		if (port != -1) {
			for (i = port; i < port + quantity; i++)
				STRESS_SETBIT(g_shared->net_port_map.allocated, i);
		}
	}
	(void)stress_lock_release(g_shared->net_port_map.lock);

	return port;
}

/*
 *   stress_net_reserve_ports()
 *	release allocated ports
 */
void stress_net_release_ports(const int start_port, const int end_port)
{
	if (LIKELY(stress_net_port_range_ok(start_port, end_port))) {
		int i;

		for (i = start_port; i <= end_port; i++)
			STRESS_CLRBIT(g_shared->net_port_map.allocated, i);
	}
}

/*
 *  stress_ipv4_checksum()
 *	ipv4 data checksum
 */
uint16_t CONST OPTIMIZE3 stress_ipv4_checksum(uint16_t *ptr, const size_t sz)
{
	register uint32_t sum = 0;
	register size_t n = sz;

	if (UNLIKELY(!ptr))
		return 0;

	while (n > 1) {
		sum += *ptr++;
		n -= 2;
	}

	if (n)
		sum += *(uint8_t*)ptr;
	sum = (sum >> 16) + (sum & 0xffff);
	sum += (sum >> 16);

	return (uint16_t)~sum;
}
