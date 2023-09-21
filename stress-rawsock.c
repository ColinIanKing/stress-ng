// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-hash.h"
#include "core-killpid.h"
#include "core-lock.h"
#include "core-out-of-memory.h"
#include "core-net.h"

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#endif

#if defined(HAVE_NETINET_IP_H)
#include <netinet/ip.h>
#endif

#define MIN_RAWSOCK_PORT		(1024)
#define MAX_RAWSOCK_PORT		(65535)
#define DEFAULT_RAWSOCK_PORT		(45000)

static const stress_help_t help[] = {
	{ NULL,	"rawsock N",		"start N workers performing raw socket send/receives " },
	{ NULL,	"rawsock-ops N",	"stop after N raw socket bogo operations" },
	{ NULL,	"rawsock-port P",	"use socket P to P + number of workers - 1" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_rawsock_port()
 *	set port to use
 */
static int stress_set_rawsock_port(const char *opt)
{
	int rawsock_port;

	stress_set_net_port("rawsock-port", opt,
		MIN_RAWSOCK_PORT, MAX_RAWSOCK_PORT, &rawsock_port);
	return stress_set_setting("rawsock-port", TYPE_ID_INT, &rawsock_port);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_rawsock_port,	stress_set_rawsock_port },
	{ 0,			NULL },
};

#if defined(SOCK_RAW) &&	\
    defined(IPPROTO_RAW) &&	\
    defined(HAVE_IPHDR) &&	\
    defined(__linux__)

typedef struct {
	struct iphdr	iph;
	uint32_t	data;
	uint32_t	hash;
} stress_raw_packet_t;

static void *rawsock_lock;
static volatile bool stop_rawsock;

static void MLOCKED_TEXT rawsock_sigalrm_handler(int signum)
{
	(void)signum;
	stop_rawsock = true;
}

static void stress_rawsock_init(void)
{
	rawsock_lock = stress_lock_create();
	stop_rawsock = false;
}

static void stress_rawsock_deinit(void)
{
	stress_lock_destroy(rawsock_lock);
}

/*
 *  stress_rawsock_supported()
 *      check if we can run this
 */
static int stress_rawsock_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_RAW)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

static int OPTIMIZE3 stress_rawsock_client(const stress_args_t *args, const int rawsock_port)
{
	/* Child, client */
	int fd;
	stress_raw_packet_t ALIGN64 pkt;
	struct sockaddr_in addr;

	stress_parent_died_alarm();
	if (stress_sighandler(args->name, SIGALRM, rawsock_sigalrm_handler, NULL) < 0)
		return EXIT_FAILURE;

	(void)sched_settings_apply(true);

	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		/* failed, kick parent to finish */
		return EXIT_FAILURE;
	}

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = rawsock_port;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	(void)shim_memset(&pkt, 0, sizeof(pkt));
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

	/* Wait for server to start */
	while (!stop_rawsock && stress_continue(args)) {
		uint32_t ready;

		if (stress_lock_acquire(rawsock_lock) < 0) {
			_exit(EXIT_FAILURE);
			}
		ready = g_shared->rawsock.ready;
		(void)stress_lock_release(rawsock_lock);

		if (ready == args->num_instances)
			break;
		shim_usleep(20000);
	}

	while (!stop_rawsock && stress_continue(args)) {
		ssize_t sret;

		pkt.hash = stress_hash_mulxror32((const char * )&pkt.data, sizeof(pkt.data));
		sret = sendto(fd, &pkt, sizeof(pkt), 0,
			(const struct sockaddr *)&addr,
			(socklen_t)sizeof(addr));
		if (UNLIKELY(sret < 0)) {
			if (errno == ENOBUFS) {
				/* Throttle */
				VOID_RET(int, nice(1));
				shim_usleep(250000);
				continue;
			}
			break;
		}
		pkt.data++;
#if defined(SIOCOUTQ)
		/* Occasionally exercise SIOCOUTQ */
		if (UNLIKELY((pkt.data & 0xff) == 0)) {
			int queued;

			if (!stress_continue(args))
				break;

			VOID_RET(int, ioctl(fd, SIOCOUTQ, &queued));
		}
#endif
	}
	(void)close(fd);
	return EXIT_SUCCESS;
}

static int OPTIMIZE3 stress_rawsock_server(const stress_args_t *args, const pid_t pid)
{
	/* Parent, server */
	int rc = EXIT_SUCCESS, fd = -1, status;
	struct sockaddr_in addr;
	double t_start, duration = 0.0, bytes = 0.0, rate;

	if (stop_rawsock || !stress_continue(args))
		goto die;

	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		(void)stress_kill_pid(pid);
		goto die;
	}

	(void)shim_memset(&addr, 0, sizeof(addr));

	(void)stress_lock_acquire(rawsock_lock);
	g_shared->rawsock.ready++;
	(void)stress_lock_release(rawsock_lock);

	t_start = stress_time_now();
	while (!stop_rawsock && stress_continue(args)) {
		stress_raw_packet_t ALIGN64 pkt;
		socklen_t len = sizeof(addr);
		ssize_t n;

		n = recvfrom(fd, &pkt, sizeof(pkt), 0,
				(struct sockaddr *)&addr, &len);
		if (UNLIKELY(n == 0)) {
			pr_inf("%s: recvfrom got zero bytes\n", args->name);
			break;
		} else if (UNLIKELY(n < 0)) {
			if (errno != EINTR) {
				pr_fail("%s: recvfrom failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}
			break;
		} else  {
			register uint32_t hash;

			bytes += n;
			hash = stress_hash_mulxror32((const char * )&pkt.data, sizeof(pkt.data));
			if (hash != pkt.hash) {
				pr_fail("%s: recv data hash check fail on "
					"data 0x%4.4" PRIx32 ", got "
					"0x%4.4" PRIx32 ", expected "
					"0x%4.4" PRIx32 "\n",
					args->name, pkt.data, hash, pkt.hash);
				rc = EXIT_FAILURE;
				break;
			}
		}
#if defined(SIOCINQ)
		/* Occasionally exercise SIOCINQ */
		if (UNLIKELY((pkt.data & 0xfff) == 0)) {
			int queued;

			if (!stress_continue(args))
				break;

			VOID_RET(int, ioctl(fd, SIOCINQ, &queued));
		}
#endif
		stress_bogo_inc(args);
	}
	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? bytes / duration : 0.0;
	stress_metrics_set(args, 0, "MB recv'd per sec", rate / (double)MB);
die:
	(void)shim_waitpid(pid, &status, 0);

	/* close recv socket after sender closed */
	if (fd > -1)
		(void)close(fd);

	return rc;
}


static int stress_rawsock_child(const stress_args_t *args, void *context)
{
	int rc = EXIT_SUCCESS, parent_cpu;
	pid_t pid;
	const int rawsock_port = *(int *)context;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno)) {
			shim_usleep(100000);
			goto again;
		}
		if (stop_rawsock || !stress_continue(args)) {
			return EXIT_SUCCESS;
		}
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		(void)stress_change_cpu(args, parent_cpu);
		rc = stress_rawsock_client(args, rawsock_port);
		_exit(rc);
	} else {
		rc = stress_rawsock_server(args, pid);
	}
	return rc;
}

/*
 *  stress_rawsock
 *	stress by heavy raw udp ops
 */
static int stress_rawsock(const stress_args_t *args)
{
	int rc, reserved_port;
	int rawsock_port = DEFAULT_RAWSOCK_PORT;

	if (!rawsock_lock) {
		pr_inf_skip("%s: failed to create rawsock lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	(void)stress_get_setting("rawsock-port", &rawsock_port);

	rawsock_port += args->instance;
	reserved_port = stress_net_reserve_ports(rawsock_port, rawsock_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, rawsock_port);
		return EXIT_NO_RESOURCE;
	}
	rawsock_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, rawsock_port);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_oomable_child(args, (void *)&rawsock_port, stress_rawsock_child, STRESS_OOMABLE_NORMAL);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(rawsock_port, rawsock_port);

	return rc;
}

stressor_info_t stress_rawsock_info = {
	.stressor = stress_rawsock,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.supported = stress_rawsock_supported,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.init = stress_rawsock_init,
	.deinit = stress_rawsock_deinit,
};
#else
stressor_info_t stress_rawsock_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/sockios.h, SOCK_RAW, IPPROTO_RAW or only supported on Linux"
};
#endif
