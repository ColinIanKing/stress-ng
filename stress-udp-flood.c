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

static const help_t help[] = {
	{ NULL,	"udp-flood N",		"start N workers that performs a UDP flood attack" },
	{ NULL,	"udp-flood-ops N",	"stop after N udp flood bogo operations" },
	{ NULL,	"udp-flood-domain D",	"specify domain, default is ipv4" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_udp_domain()
 *      set the udp domain option
 */
static int stress_set_udp_flood_domain(const char *name)
{
	int ret, udp_flood_domain;

	ret = stress_set_net_domain(DOMAIN_INET_ALL, "udp-flood-domain",
		name, &udp_flood_domain);
	set_setting("udp-flood-domain", TYPE_ID_INT, &udp_flood_domain);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_udp_flood_domain,	stress_set_udp_flood_domain },
	{ 0,			NULL }
};

#if defined(AF_PACKET)

/*
 *  stress_udp_flood
 *	UDP flood
 */
static int stress_udp_flood(const args_t *args)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	int udp_flood_domain = AF_INET;
	int port = 1024;
	struct sockaddr *addr;
	socklen_t addr_len;
	const size_t sz_max = 23 + args->instance;
	size_t sz = 1;

	static const char data[64] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUV"
		"WXYZabcdefghijklmnopqrstuvwxyz@!";

	(void)get_setting("udp-flood-domain", &udp_flood_domain);

	if ((fd = socket(udp_flood_domain, SOCK_DGRAM, AF_PACKET)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_inf("%s: skipping stressor, protocol not supported\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail_dbg("socket");
		return EXIT_FAILURE;
	}
	stress_set_sockaddr(args->name, args->instance, args->pid,
		udp_flood_domain, port,
		&addr, &addr_len, NET_ADDR_ANY);

	do {
		char buf[sz];
		int rand_port;

		(void)memset(buf, data[j++ & 63], sz);

		stress_set_sockaddr_port(udp_flood_domain, port, addr);
		if (sendto(fd, buf, sz, 0, addr, addr_len) > 0)
			inc_counter(args);
		if (++port > 65535)
			port = 1024;

		if (!keep_stressing())
			break;

		rand_port = 1024 + (mwc16() % (65535 - 1024));
		stress_set_sockaddr_port(udp_flood_domain, rand_port, addr);
		if (sendto(fd, buf, sz, 0, addr, addr_len) > 0)
			inc_counter(args);

		if (++sz > sz_max)
			sz = 1;
	} while (keep_stressing());

	(void)close(fd);

	return rc;
}

stressor_info_t stress_udp_flood_info = {
	.stressor = stress_udp_flood,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_udp_flood_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
