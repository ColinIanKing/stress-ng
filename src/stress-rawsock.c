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
	{ NULL,	"rawsock N",		"start N workers performing raw socket send/receives " },
	{ NULL,	"rawsock-ops N",	"stop after N raw socket bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(SOCK_RAW) &&	\
    defined(IPPROTO_RAW) &&	\
    defined(HAVE_ICMPHDR) &&	\
    defined(__linux__)

typedef struct {
	struct iphdr	iph;
	uint32_t	data;
} stress_raw_packet_t;

/*
 *  stress_rawsock_supported()
 *      check if we can run this
 */
static int stress_rawsock_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_RAW)) {
		pr_inf("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  stress_rawsock
 *	stress by heavy raw udp ops
 */
static int stress_rawsock(const stress_args_t *args)
{
	pid_t pid;
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (keep_stressing_flag() && (errno == EAGAIN))
			goto again;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		/* Child, client */
		int fd;
		stress_raw_packet_t pkt;
		struct sockaddr_in addr;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			/* failed, kick parent to finish */
			(void)kill(getppid(), SIGALRM);
			_exit(EXIT_FAILURE);
		}

		(void)memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = 0;
		addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

		(void)memset(&pkt, 0, sizeof(pkt));
		pkt.iph.version = 4;
		pkt.iph.ihl = sizeof(struct iphdr) >> 2;
		pkt.iph.tos = 0;
		pkt.iph.tot_len = htons(40);
		pkt.iph.id = 0;
		pkt.iph.ttl = 64;
		pkt.iph.protocol = IPPROTO_RAW;
		pkt.iph.frag_off = 0;
		pkt.iph.check = 0;
		pkt.iph.saddr = addr.sin_addr.s_addr;
		pkt.iph.daddr = addr.sin_addr.s_addr;

		do {
			rc = sendto(fd, &pkt, sizeof(pkt), 0,
				(const struct sockaddr *)&addr,
				(socklen_t)sizeof(addr));
			if (rc < 0)
				break;
			pkt.data++;
#if defined(SIOCOUTQ)
			/* Occasionally exercise SIOCINQ */
			if ((pkt.data & 0xff) == 0) {
				int ret, queued;

				ret = ioctl(fd, SIOCOUTQ, &queued);
				(void)ret;
			}
#endif
		} while (keep_stressing(args));
		(void)close(fd);

		(void)kill(getppid(), SIGALRM);
		_exit(EXIT_SUCCESS);
	} else {
		/* Parent, server */
		int fd, status;
		struct sockaddr_in addr;

		(void)setpgid(pid, g_pgrp);

		if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
			rc = EXIT_FAILURE;
			goto die;
		}
		if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto die;
		}

		(void)memset(&addr, 0, sizeof(addr));

		do {
			stress_raw_packet_t pkt;
			socklen_t len = sizeof(addr);
			ssize_t n;

			n = recvfrom(fd, &pkt, sizeof(pkt), 0,
					(struct sockaddr *)&addr, &len);
			if (UNLIKELY(n == 0)) {
				break;
			} else if (UNLIKELY(n < 0)) {
				if (errno != EINTR)
					pr_fail("%s: recvfrom failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				break;
			}

#if defined(SIOCINQ)
			/* Occasionally exercise SIOCINQ */
			if ((pkt.data & 0xff) == 0) {
				int ret, queued;

				ret = ioctl(fd, SIOCINQ, &queued);
				(void)ret;
			}
#endif
			inc_counter(args);
		} while (keep_stressing(args));

		(void)close(fd);
die:
		stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
		if (pid) {
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		}
	}
	return rc;
}

stressor_info_t stress_rawsock_info = {
	.stressor = stress_rawsock,
	.class = CLASS_NETWORK | CLASS_OS,
	.supported = stress_rawsock_supported,
	.help = help
};
#else
stressor_info_t stress_rawsock_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#endif
