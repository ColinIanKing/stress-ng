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
#include "core-builtin.h"
#include "core-net.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#else
UNEXPECTED
#endif

#define MAX_UDP_SIZE	(2048)

static const stress_help_t help[] = {
	{ NULL,	"udp-flood N",		"start N workers that performs a UDP flood attack" },
	{ NULL,	"udp-flood-domain D",	"specify domain, default is ipv4" },
	{ NULL, "udp-flood-if I",	"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"udp-flood-ops N",	"stop after N udp flood bogo operations" },
	{ NULL,	NULL,			NULL }
};

static int udp_domain_mask = DOMAIN_INET_ALL;

static const stress_opt_t opts[] = {
	{ OPT_udp_flood_domain,	"udp-flood-domain", TYPE_ID_INT_DOMAIN, 0, 0, &udp_domain_mask },
	{ OPT_udp_flood_if,	"udp-flood-if",     TYPE_ID_STR, 0, 0, NULL },
	END_OPT,
};

#if defined(AF_PACKET)

/*
 *  stress_udp_flood
 *	UDP flood
 */
static int OPTIMIZE3 stress_udp_flood(stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS, j = 0;
	int udp_flood_domain = AF_INET;
	int port = 1024;
	struct sockaddr *addr;
	socklen_t addr_len;
	const size_t sz_max = STRESS_MINIMUM(23 + args->instance, MAX_UDP_SIZE);
	size_t sz = 1;
	char *udp_flood_if = NULL;
	double bytes = 0.0, duration, t, rate;
	uint64_t sendto_failed = 0, total_count;

	(void)stress_get_setting("udp-flood-domain", &udp_flood_domain);
	(void)stress_get_setting("udp-flood-if", &udp_flood_if);

	if (udp_flood_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(udp_flood_if, udp_flood_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, udp_flood_if, stress_net_domain(udp_flood_domain));
			udp_flood_if = NULL;
		}
	}

	if ((fd = socket(udp_flood_domain, SOCK_DGRAM, AF_PACKET)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			if (stress_instance_zero(args))
				pr_inf_skip("%s: skipping stressor, protocol not supported\n",
					args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	if (stress_set_sockaddr_if(args->name, args->instance,
			args->pid,
			udp_flood_domain, port, udp_flood_if,
			&addr, &addr_len, NET_ADDR_ANY) < 0) {
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		char buf[MAX_UDP_SIZE];
		int rand_port, reserved_port;
		ssize_t n;

		if (UNLIKELY(++port > 65535))
			port = 1024;

		reserved_port = stress_net_reserve_ports(port, port);
		if (UNLIKELY(reserved_port < 0))
			continue;
		port = reserved_port;

		stress_set_sockaddr_port(udp_flood_domain, port, addr);
		(void)shim_memset(buf, stress_ascii64[j++ & 63], sz);
		n = sendto(fd, buf, sz, 0, addr, addr_len);
		if (LIKELY(n > 0)) {
			stress_bogo_inc(args);
			bytes += (double)n;
		} else {
			sendto_failed++;
		}

#if defined(SIOCOUTQ)
		if (UNLIKELY((port & 0x1f) == 0)) {
			int pending;

			VOID_RET(int, ioctl(fd, SIOCOUTQ, &pending));
		}
#else
		UNEXPECTED
#endif
		stress_net_release_ports(port, port);

		if (UNLIKELY(!stress_continue(args)))
			break;

		rand_port = 1024 + stress_mwc16modn(65535 - 1024);
		reserved_port = stress_net_reserve_ports(rand_port, rand_port);
		if (UNLIKELY(reserved_port < 0))
			continue;
		rand_port = reserved_port;
		stress_set_sockaddr_port(udp_flood_domain, rand_port, addr);
		n = sendto(fd, buf, sz, 0, addr, addr_len);
		if (LIKELY(n > 0)) {
			stress_bogo_inc(args);
			bytes += (double)n;
		} else {
			sendto_failed++;
		}
		stress_net_release_ports(rand_port, rand_port);
		if (UNLIKELY(++sz >= sz_max))
			sz = 1;
	} while (stress_continue(args));

	duration = stress_time_now() - t;

	rate = (duration > 0.0) ? (bytes / duration) / (double)MB : 0.0;
	stress_metrics_set(args, 0, "MB per sec sendto rate",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = (duration > 0.0) ? (stress_bogo_get(args) / duration) : 0.0;
	stress_metrics_set(args, 1, "sendto calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	total_count = stress_bogo_get(args) + sendto_failed;
	rate = (total_count > 0) ? ((total_count - sendto_failed) / total_count) * 100.0 : 0.0;
	stress_metrics_set(args, 2, "% sendto calls succeeded",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	/* 100% sendto failure is not good */
	if ((total_count > 0) && (sendto_failed == total_count)) {
		pr_fail("%s: 100%% of %" PRIu64 " sendto calls failed\n",
			args->name, total_count);
		rc = EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return rc;
}

const stressor_info_t stress_udp_flood_info = {
	.stressor = stress_udp_flood,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_udp_flood_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built with undefined AF_PACKET"
};
#endif
