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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-killpid.h"
#include "core-net.h"

#include <sys/ioctl.h>

#if defined(HAVE_LINUX_IF_PACKET_H)
#include <linux/if_packet.h>
#endif

#if defined(HAVE_LINUX_IF_TUN_H)
#include <linux/if_tun.h>
#endif

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#endif

#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif

#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif

#if defined(HAVE_NETINET_UDP_H)
#include <netinet/udp.h>
#elif defined(HAVE_LINUX_UDP_H)
#include <linux/udp.h>
#endif

#include <arpa/inet.h>

#define DEFAULT_RAWPKT_PORT	(14000)

#if !defined(SOL_UDP)
#define SOL_UDP 	(17)
#endif
#define PACKET_SIZE	(2048)

static const stress_help_t help[] = {
	{ NULL, "rawpkt N",		"start N workers exercising raw packets" },
	{ NULL,	"rawpkt-ops N",		"stop after N raw packet bogo operations" },
	{ NULL,	"rawpkt-port P",	"use raw packet ports P to P + number of workers - 1" },
	{ NULL, "rawpkt-rxring N",	"setup raw packets with RX ring with N number of blocks, this selects TPACKET_V3"},
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_rawpkt_supported()
 *      check if we can run this as root
 */
static int stress_rawpkt_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_RAW)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

static const stress_opt_t opts[] = {
	{ OPT_rawpkt_port,   "rawpkt-port",   TYPE_ID_INT_PORT, MIN_PORT, MAX_PORT, NULL },
	{ OPT_rawpkt_rxring, "rawpkt-rxring", TYPE_ID_INT, 1, 16, NULL },
	END_OPT,
};

#if defined(HAVE_LINUX_UDP_H) &&	\
    defined(HAVE_LINUX_IF_PACKET_H) &&	\
    defined(HAVE_IFREQ)
/*
 *  stress_rawpkt_sockopts()
 *	fetch some SOL_PACKET specific stats, ignore failures
 *	just exercise the interface.
 */
static void stress_rawpkt_sockopts(const int fd)
{
#if defined(PACKET_STATISTICS)
	{
		struct tpacket_stats stats;
		socklen_t len = sizeof(stats);

		(void)getsockopt(fd, SOL_PACKET, PACKET_STATISTICS, &stats, &len);
	}
#endif
#if defined(PACKET_AUXDATA)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_AUXDATA, &val, len);
	}
#endif
#if defined(PACKET_ORIGDEV)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_VNET_HDR)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_VNET_HDR, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_VERSION)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_VERSION, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_HDRLEN)
	{
		static const int vals[] = { 0, 1, 2 };
		int val = (int)stress_mwc32modn(SIZEOF_ARRAY(vals));
		socklen_t len = sizeof(val);

		(void)getsockopt(fd, SOL_PACKET, PACKET_HDRLEN, &val, &len);
	}
#endif
#if defined(PACKET_RESERVE)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_RESERVE, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_LOSS)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_LOSS, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_TIMESTAMP)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_TIMESTAMP, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_FANOUT)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_FANOUT, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_IGNORE_OUTGOING)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_IGNORE_OUTGOING, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_ROLLOVER_STATS)
	{
		struct tpacket_rollover_stats rstats;
		socklen_t len = sizeof(rstats);

		(void)getsockopt(fd, SOL_PACKET, PACKET_ROLLOVER_STATS, &rstats, &len);
	}
#endif
#if defined(PACKET_TX_HAS_OFF)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_TX_HAS_OFF, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
#if defined(PACKET_QDISC_BYPASS)
	{
		int ret, val;
		socklen_t len = sizeof(val);

		ret = getsockopt(fd, SOL_PACKET, PACKET_QDISC_BYPASS, &val, &len);
		if (ret == 0)
			(void)setsockopt(fd, SOL_PACKET, PACKET_ORIGDEV, &val, len);
	}
#endif
	{
		int val;
		socklen_t len = sizeof(val);

		(void)getsockopt(fd, SOL_PACKET, ~(int)0, &val, &len);
	}
}

/*
 *  stress_rawpkt_client()
 *	client sender
 */
static void NORETURN OPTIMIZE3 stress_rawpkt_client(
	stress_args_t *args,
	struct ifreq *hwaddr,
	struct ifreq *ifaddr,
	const struct ifreq *idx,
	const int port)
{
	int rc = EXIT_FAILURE;
	uint16_t id = 12345;
	uint32_t buf[PACKET_SIZE / sizeof(uint32_t)] ALIGN64;
	struct ethhdr *eth = (struct ethhdr *)buf;
	struct iphdr *ip = (struct iphdr *)((uintptr_t)buf + sizeof(struct ethhdr));
	struct udphdr *udp = (struct udphdr *)((uintptr_t)buf + sizeof(struct ethhdr) + sizeof(struct iphdr));
	struct sockaddr_ll sadr;
	int fd;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	(void)shim_memset(&sadr, 0, sizeof(sadr));
	(void)shim_memset(buf, 0, sizeof(buf));

	(void)shim_memcpy(eth->h_dest, hwaddr->ifr_addr.sa_data, sizeof(eth->h_dest));
	(void)shim_memcpy(eth->h_source, hwaddr->ifr_addr.sa_data, sizeof(eth->h_dest));
	eth->h_proto = htons(ETH_P_IP);

	ip->ihl = 5;		/* Header length in 32 bit words */
	ip->version = 4;	/* IPv4 */
	ip->tos = 0;
	ip->tot_len = sizeof(struct iphdr) + sizeof(struct udphdr);
	ip->ttl = 16;  		/* Not too many hops! */
	ip->protocol = SOL_UDP;	/* UDP protocol */
	ip->saddr = inet_addr(inet_ntoa((((struct sockaddr_in *)&(ifaddr->ifr_addr))->sin_addr)));
	ip->daddr = ip->saddr;

	udp->source = htons(port);
	udp->dest = htons(port);
	udp->len = htons(sizeof(struct udphdr));

	sadr.sll_ifindex = idx->ifr_ifindex;
	sadr.sll_halen = ETH_ALEN;
	(void)shim_memcpy(&sadr.sll_addr, eth->h_dest, sizeof(eth->h_dest));

	if ((fd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto err;
	}

	do {
		ssize_t n;

		ip->id = htons(id++);
		ip->check = stress_ipv4_checksum((uint16_t *)ip, sizeof(struct iphdr) + sizeof(struct udphdr));

		n = sendto(fd, buf, sizeof(struct ethhdr) + ip->tot_len, 0, (struct sockaddr *)&sadr, sizeof(sadr));
		if (UNLIKELY(n < 0)) {
			pr_fail("%s: raw socket sendto failed on port %d, errno=%d (%s)\n",
				args->name, port, errno, strerror(errno));
		}
#if defined(SIOCOUTQ)
		/* Occasionally exercise SIOCOUTQ */
		if (UNLIKELY((id & 0xff) == 0)) {
			int queued;

			VOID_RET(int, ioctl(fd, SIOCOUTQ, &queued));
		}
#endif
	} while (stress_continue(args));

	stress_rawpkt_sockopts(fd);
	(void)close(fd);

	rc = EXIT_SUCCESS;
err:
	_exit(rc);
}

/*
 *  stress_rawpkt_server()
 *	server reader
 */
static int OPTIMIZE3 stress_rawpkt_server(
	stress_args_t *args,
	struct ifreq *ifaddr,
	const int port,
	const int blocknr)
{
	int fd;
	int rc = EXIT_SUCCESS;
	uint32_t buf[PACKET_SIZE / sizeof(uint32_t)];
	const struct ethhdr *eth = (struct ethhdr *)buf;
	const struct iphdr *ip = (struct iphdr *)((uintptr_t)buf + sizeof(struct ethhdr));
	const struct udphdr *udp = (struct udphdr *)((uintptr_t)buf + sizeof(struct ethhdr) + sizeof(struct iphdr));
	struct sockaddr saddr;
	int saddr_len = sizeof(saddr);
	const in_addr_t addr = inet_addr(inet_ntoa((((struct sockaddr_in *)&(ifaddr->ifr_addr))->sin_addr)));
	uint64_t all_pkts = 0;
	const ssize_t min_size = sizeof(struct ethhdr) + sizeof(struct iphdr) + sizeof(struct udphdr);
	double t_start, duration, bytes = 0.0, rate;

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}

#if defined(PACKET_RX_RING) &&	\
    defined(PACKET_VERSION) &&	\
    defined(HAVE_TPACKET_REQ3)
	if (blocknr) {
		struct tpacket_req3 tp;
		int val = TPACKET_V3;

		if (setsockopt(fd, SOL_PACKET, PACKET_VERSION, &val, sizeof(val)) < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: setsockopt failed to set packet version, errno=%d (%s)\n", args->name, errno, strerror(errno));
			goto close_fd;
		}
		(void)shim_memset(&tp, 0, sizeof(tp));
		tp.tp_block_size = getpagesize();
		tp.tp_block_nr = blocknr;
		tp.tp_frame_size = getpagesize() / blocknr;
		tp.tp_frame_nr = tp.tp_block_size / tp.tp_frame_size * blocknr;

		if (setsockopt(fd, SOL_PACKET, PACKET_RX_RING, (void*) &tp, sizeof(tp)) < 0) {
			rc = stress_exit_status(errno);
			pr_fail("%s: setsockopt failed to set rx ring, errno=%d (%s)\n", args->name, errno, strerror(errno));
			goto close_fd;
		}
	}
#else
	(void)blocknr;
#endif

	t_start = stress_time_now();
	do {
		ssize_t n;

		n = recvfrom(fd, buf, sizeof(buf), 0, &saddr, (socklen_t *)&saddr_len);
		if (LIKELY(n >= min_size)) {
			all_pkts++;
			if ((eth->h_proto == htons(ETH_P_IP)) &&
			    ((in_addr_t)ip->saddr == addr) &&
			    (ip->protocol == SOL_UDP) &&
			    (ntohs(udp->source) == port)) {
				stress_bogo_inc(args);
				bytes += (double)n;
			}
		}
#if defined(SIOCINQ)
		/* Exercise SIOCINQ */
		if (UNLIKELY((all_pkts & 0xff) == 0)) {
			int queued;

			VOID_RET(int, ioctl(fd, SIOCINQ, &queued));
		}
#endif
	} while (stress_continue(args));

	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB recv'd per sec",
		rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);
	stress_metrics_set(args, 1, "packets sent",
		(double)stress_bogo_get(args), STRESS_METRIC_TOTAL);
	stress_metrics_set(args, 2, "packets received",
		(double)all_pkts, STRESS_METRIC_TOTAL);

	stress_rawpkt_sockopts(fd);
#if defined(PACKET_RX_RING) &&	\
    defined(PACKET_VERSION) &&	\
    defined(HAVE_TPACKET_REQ3)
close_fd:
#endif
	(void)close(fd);
die:
	return rc;
}

static void stress_sock_sigpipe_handler(int signum)
{
	(void)signum;

	stress_continue_set_flag(false);
}

/*
 *  stress_rawpkt
 *	stress raw socket I/O UDP packet send/receive
 */
static int stress_rawpkt(stress_args_t *args)
{
	pid_t pid;
	int reserved_port, rawpkt_port = DEFAULT_RAWPKT_PORT;
	int fd, rc = EXIT_FAILURE, parent_cpu;
	struct ifreq hwaddr, ifaddr, idx;
	int rawpkt_rxring = 0;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("rawpkt-port", &rawpkt_port);
	(void)stress_get_setting("rawpkt-rxring", &rawpkt_rxring);

	if ((rawpkt_rxring & (rawpkt_rxring - 1)) != 0) {
		(void)pr_inf("%s: --rawpkt-rxing value %d is not "
			"a power of 2, disabling option\n", args->name, rawpkt_rxring);
		rawpkt_rxring = 0;
	}
	rawpkt_port += args->instance;
	if (rawpkt_port > MAX_PORT)
		rawpkt_port -= (MAX_PORT - MIN_PORT + 1); /* Wrap round */
	reserved_port = stress_net_reserve_ports(rawpkt_port, rawpkt_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, rawpkt_port);
		return EXIT_NO_RESOURCE;
	}
	rawpkt_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, rawpkt_port);

	if (stress_sighandler(args->name, SIGPIPE, stress_sock_sigpipe_handler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}
	(void)shim_memset(&hwaddr, 0, sizeof(hwaddr));
	(void)shim_strscpy(hwaddr.ifr_name, "lo", sizeof(hwaddr.ifr_name));
	if (ioctl(fd, SIOCGIFHWADDR, &hwaddr) < 0) {
		pr_fail("%s: ioctl SIOCGIFHWADDR on lo failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_FAILURE;
	}

	(void)shim_memset(&ifaddr, 0, sizeof(ifaddr));
	(void)shim_strscpy(ifaddr.ifr_name, "lo", sizeof(ifaddr.ifr_name));
	if (ioctl(fd, SIOCGIFADDR, &ifaddr) < 0) {
		pr_fail("%s: ioctl SIOCGIFADDR on lo failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_FAILURE;
	}

	(void)shim_memset(&idx, 0, sizeof(idx));
	(void)shim_strscpy(idx.ifr_name, "lo", sizeof(idx.ifr_name));
	if (ioctl(fd, SIOCGIFINDEX, &idx) < 0) {
		pr_fail("%s: ioctl SIOCGIFINDEX on lo failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_FAILURE;
	}
	(void)close(fd);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args))) {
			rc = EXIT_SUCCESS;
			goto finish;
		}
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_rawpkt_client(args, &hwaddr, &ifaddr, &idx, rawpkt_port);
	} else {
		rc = stress_rawpkt_server(args, &ifaddr, rawpkt_port, rawpkt_rxring);
		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_rawpkt_info = {
	.stressor = stress_rawpkt,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.supported = stress_rawpkt_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_rawpkt_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.supported = stress_rawpkt_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/if_packet.h, linux/if_tun.h, linux/sockios.h or linux/udp.h"
};
#endif
