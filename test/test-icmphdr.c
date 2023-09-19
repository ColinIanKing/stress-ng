// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King
 *
 */

#include <netinet/ip.h>
#include <netinet/ip_icmp.h>

int main(void)
{
	struct iphdr my_iphdr = { };
	struct icmphdr my_icmphdr = { };

	(void)my_iphdr;
	(void)my_icmphdr;

	return sizeof(struct iphdr) + sizeof(struct icmphdr);
}
