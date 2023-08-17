/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */
#ifndef CORE_NET_H
#define CORE_NET_H

/* Network domains flags */
#define DOMAIN_INET		(0x00000001)	/* AF_INET */
#define DOMAIN_INET6		(0x00000002)	/* AF_INET6 */
#define DOMAIN_UNIX		(0x00000004)	/* AF_UNIX */

#define DOMAIN_INET_ALL		(DOMAIN_INET | DOMAIN_INET6)
#define DOMAIN_ALL		(DOMAIN_INET | DOMAIN_INET6 | DOMAIN_UNIX)

#define NET_ADDR_ANY		(0)
#define NET_ADDR_LOOPBACK	(1)

/* Network helpers */
extern void stress_set_net_port(const char *optname, const char *opt,
	const int min_port, const int max_port, int *port);
extern WARN_UNUSED int stress_set_net_domain(const int domain_mask,
	const char *name, const char *domain_name, int *domain);
extern WARN_UNUSED int stress_set_sockaddr_if(const char *name, const uint32_t instance,
        const pid_t pid, const int domain, const int port, const char *ifname,
	struct sockaddr **sockaddr, socklen_t *len, const int net_addr);
extern WARN_UNUSED int stress_set_sockaddr(const char *name, const uint32_t instance,
	const pid_t pid, const int domain, const int port,
	struct sockaddr **sockaddr, socklen_t *len, const int net_addr);
extern void stress_set_sockaddr_port(const int domain, const int port,
	struct sockaddr *sockaddr);
extern int stress_net_interface_exists(const char *interface, const int domain, struct sockaddr *addr);
extern WARN_UNUSED const char *stress_net_domain(const int domain);

extern WARN_UNUSED int stress_net_reserve_ports(const int start_port, const int end_port);
extern void stress_net_release_ports(const int start_port, const int end_port);

#endif
