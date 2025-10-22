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

#if defined(HAVE_LINUX_IF_TUN_H)
#include <linux/if_tun.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#else
UNEXPECTED
#endif

#include <arpa/inet.h>

#define PACKETS_TO_SEND		(64)

static const stress_help_t help[] = {
	{ NULL,	"tun N",	"start N workers exercising tun interface" },
	{ NULL,	"tun-ops N",	"stop after N tun bogo operations" },
	{ NULL, "tun-tap",	"use TAP interface instead of TUN" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_tun_tap, "tun-tap", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_LINUX_IF_TUN_H) &&	\
    defined(HAVE_IFREQ) &&		\
    defined(IFF_TUN) &&			\
    defined(TUNSETIFF) && 		\
    defined(TUNSETOWNER) && 		\
    defined(TUNSETGROUP) && 		\
    defined(TUNSETPERSIST)

static const char tun_dev[] = "/dev/net/tun";

/*
 *  stress_tun_supported()
 *      check if we can run this
 */
static int stress_tun_supported(const char *name)
{
	int fd;

	if (!stress_check_capability(SHIM_CAP_NET_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n", name);
		return -1;
	}

	fd = open(tun_dev, O_RDWR);
	if (fd < 0) {
		pr_inf_skip("%s stressor will be skipped, cannot open %s\n", name, tun_dev);
		return -1;

	}
	(void)close(fd);

	return 0;
}

/*
 *  stress_tun
 *	stress tun interface
 */
static int stress_tun(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	const uid_t owner = geteuid();
	const gid_t group = getegid();
	char ip_addr[32];
	bool tun_tap = false;

	(void)stress_get_setting("tun-tap", &tun_tap);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i, fd, sfd, ret, status, parent_cpu;
		pid_t pid;
		struct ifreq ifr;
		struct sockaddr_in *tun_addr;
		int port = 2000 + (stress_mwc16() & 0xfff);

		port = stress_net_reserve_ports(port, port);
		if (UNLIKELY(port < 0))
			continue;	/* try again */

		fd = open(tun_dev, O_RDWR);
		if (UNLIKELY(fd < 0)) {
			pr_fail("%s: cannot open %s, errno=%d (%s)\n",
				args->name, tun_dev, errno, strerror(errno));
			stress_net_release_ports(port, port);
			return EXIT_FAILURE;
		}

		(void)shim_memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_flags = tun_tap ? IFF_TAP : IFF_TUN;

		ret = ioctl(fd, TUNSETIFF, (void *)&ifr);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: ioctl TUNSETIFF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			stress_net_release_ports(port, port);
			rc = EXIT_FAILURE;
			break;
		}

		ret = ioctl(fd, TUNSETOWNER, owner);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: ioctl TUNSETOWNER failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto clean_up;
		}

		ret = ioctl(fd, TUNSETGROUP, group);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: ioctl TUNSETGROUP failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto clean_up;
		}

		sfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (UNLIKELY(sfd < 0))
			goto clean_up;
		ifr.ifr_addr.sa_family = AF_INET;
		tun_addr = (struct sockaddr_in *)&ifr.ifr_addr;

		/*
		 *  Attempt to assign some kind of random address
		 */
		for (i = 0; i < 32; i++) {
			(void)snprintf(ip_addr, sizeof(ip_addr), "192.168.%" PRIu8 ".%" PRIu8,
				(uint8_t)((stress_mwc8modn(252)) + 2),
				(uint8_t)((stress_mwc8modn(254)) + 1));

			(void)inet_pton(AF_INET, ip_addr, &tun_addr->sin_addr);
			ret = ioctl(sfd, SIOCSIFADDR, &ifr);
			if (LIKELY(ret == 0))
				break;
		}
		(void)close(sfd);
		if (UNLIKELY(ret < 0))
			goto clean_up;

		parent_cpu = stress_get_cpu();
		pid = fork();
		if (pid < 0) {
			goto clean_up;
		} else if (pid == 0) {
			/* Child */

			struct sockaddr_in addr;
			socklen_t len;
			ssize_t n;
			char buffer[4];

			stress_set_proc_state(args->name, STRESS_STATE_RUN);
			(void)stress_change_cpu(args, parent_cpu);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);

			sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (UNLIKELY(sfd < 0)) {
				switch (errno) {
				case EMFILE:
				case ENFILE:
				case ENOBUFS:
				case ENOMEM:
					rc = EXIT_NO_RESOURCE;
					break;
				case EINTR:
					rc = EXIT_SUCCESS;
					break;
				default:
					pr_dbg("%s: child socket failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
				goto child_cleanup_fd;
			}

			(void)shim_memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = (in_port_t)port;
			len = sizeof(addr);
			inet_pton(AF_INET, ip_addr, &addr.sin_addr.s_addr);

			ret = bind(sfd, (struct sockaddr *)&addr, len);
			if (UNLIKELY(ret < 0)) {
				switch (errno) {
				case EADDRINUSE:
				case ENOMEM:
					rc = EXIT_NO_RESOURCE;
					break;
				case EINTR:
					rc = EXIT_SUCCESS;
					break;
				default:
					pr_dbg("%s: child bind failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
				goto child_cleanup;
			}

			for (i = 0; i < PACKETS_TO_SEND; i++) {
				n = recvfrom(sfd, buffer, sizeof(buffer), 0,
					(struct sockaddr *)&addr, &len);
				if (UNLIKELY(n < 0))
					break;
			}
child_cleanup:
			(void)close(sfd);
child_cleanup_fd:
			(void)close(fd);
			_exit(rc);
		} else {
			/* Parent */
			struct sockaddr_in addr;
			socklen_t len;
			static const char buffer[] = "test";
			ssize_t n;

			ret = ioctl(fd, TUNSETPERSIST, 0);
			if (UNLIKELY(ret < 0)) {
				pr_fail("%s: ioctl TUNSETPERSIST failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				(void)close(fd);
				break;
			}
#if defined(TUNSETNOCSUM)
			VOID_RET(int, ioctl(fd, TUNSETNOCSUM, 1));
#else
			UNEXPECTED
#endif

#if defined(SIOCGIFHWADDR)
			VOID_RET(int, ioctl(fd, SIOCGIFHWADDR, &ifr));
#else
			UNEXPECTED
#endif

#if defined(TUNGETVNETHDRSZ)
			{
				int vnet_hdr_sz;

				ret = ioctl(fd, TUNGETVNETHDRSZ, &vnet_hdr_sz);
				if (ret == 0) {
#if defined(TUNSETVNETHDRSZ)
					VOID_RET(int, ioctl(fd, TUNSETVNETHDRSZ, &vnet_hdr_sz));
#else
					UNEXPECTED
#endif
				}
			}
#else
			UNEXPECTED
#endif

#if defined(TUNGETSNDBUF)
			{
				int sndbuf;

				ret = ioctl(fd, TUNGETSNDBUF, &sndbuf);
				if (ret == 0) {
#if defined(TUNSETVNETHDRSZ)
					VOID_RET(int, ioctl(fd, TUNSETSNDBUF, &sndbuf));
#else
				UNEXPECTED
#endif
				}
			}
#else
			UNEXPECTED
#endif

#if defined(TUNGETVNETLE)
			{
				int val;

				ret = ioctl(fd, TUNGETVNETLE, &val);
				if (ret == 0) {
#if defined(TUNSETVNETLE)
					VOID_RET(int, ioctl(fd, TUNSETVNETLE, &val));
#else
				UNEXPECTED
#endif
				}
			}
#else
			UNEXPECTED
#endif

#if defined(TUNGETVNETBE)
			{
				int val;

				ret = ioctl(fd, TUNGETVNETBE, &val);
				if (ret == 0) {
#if defined(TUNSETVNETBE)
					VOID_RET(int, ioctl(fd, TUNSETVNETBE, &val));
#else
				UNEXPECTED
#endif
				}
			}
#else
			UNEXPECTED
#endif

#if defined(TUNGETDEVNETNS)
			VOID_RET(int, ioctl(fd, TUNGETDEVNETNS, NULL /* not required */));
#else
			UNEXPECTED
#endif

			sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (UNLIKELY(sfd < 0)) {
				pr_fail("%s: parent socket failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto child_reap;
			}

			(void)shim_memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = (in_port_t)port;
			len = sizeof(addr);
			inet_pton(AF_INET, ip_addr, &addr.sin_addr.s_addr);

			for (i = 0; LIKELY(stress_continue(args) && (i < PACKETS_TO_SEND)); i++) {
				n = sendto(sfd, buffer, sizeof(buffer), 0,
					(struct sockaddr *)&addr, len);
				if (UNLIKELY(n < 0))
					break;
				(void)shim_sched_yield();
			}
			(void)close(sfd);
		}
child_reap:
		(void)stress_kill_pid_wait(pid, &status);
		if (WEXITSTATUS(status) == EXIT_FAILURE)
			pr_fail("%s: child reading process failed\n", args->name);

clean_up:
		(void)close(fd);
		stress_net_release_ports(port, port);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);


	return rc;
}

const stressor_info_t stress_tun_info = {
	.stressor = stress_tun,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.supported = stress_tun_supported,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_tun_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/if_tun.h and various undefined TUN related macros"
};
#endif
