/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"ping-sock N",		"start N workers that exercises a ping socket" },
	{ NULL,	"ping-sock-ops N",	"stop after N ping sendto messages" },
	{ NULL,	NULL,			NULL }
};

#if defined(PF_INET) &&		\
    defined(SOCK_DGRAM) &&	\
    defined(IPPROTO_ICMP) &&	\
    defined(HAVE_ICMPHDR) &&	\
    defined(__linux__)

/*
 *  stress_ping_sock
 *	UDP flood
 */
static int stress_ping_sock(const stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	struct sockaddr_in addr;
	struct icmphdr *icmp_hdr;
	const size_t sz = 4;
	int rand_port;
	char ALIGN64 buf[sizeof(*icmp_hdr) + sz];

	static const char data[64] =
		"0123456789ABCDEFGHIJKLMNOPQRSTUV"
		"WXYZabcdefghijklmnopqrstuvwxyz@!";

	if ((fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_ICMP)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_inf("%s: skipping stressor, protocol not supported\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		if ((errno == EPERM) || (errno == EACCES)) {
			pr_inf("%s: skipping stressor, permission denied\n",
				args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	icmp_hdr = (struct icmphdr *)buf;
	(void)memset(icmp_hdr, 0, sizeof(*icmp_hdr));
	icmp_hdr->type = ICMP_ECHO;
	icmp_hdr->un.echo.id = getpid();	/* some unique ID */
	icmp_hdr->un.echo.sequence = 1;

	rand_port = 1024 + (stress_mwc16() % (65535 - 1024));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)memset(buf + sizeof(*icmp_hdr), data[j++ & 63], sz);
		addr.sin_port = htons(rand_port);

		if (sendto(fd, buf, sizeof(buf), 0, (struct sockaddr *)&addr, sizeof(addr)) > 0)
			inc_counter(args);

		icmp_hdr->un.echo.sequence++;
		rand_port++;
		if (rand_port > 65535)
			rand_port = 0;
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return rc;
}

stressor_info_t stress_ping_sock_info = {
	.stressor = stress_ping_sock,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_ping_sock_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#endif
