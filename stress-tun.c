/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

#define PACKETS_TO_SEND		(64)

static const stress_help_t help[] = {
	{ NULL,	"tun N",	"start N workers exercising tun interface" },
	{ NULL,	"tun-ops N",	"stop after N tun bogo operations" },
	{ NULL, "tun-tap",	"use TAP interface instead of TUN" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LINUX_IF_TUN_H) &&	\
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
static int stress_tun_supported(void)
{
	int fd;

	if (!stress_check_capability(SHIM_CAP_NET_ADMIN)) {
		pr_inf("tun stressor will be skipped, "
			"need to be running with CAP_NET_RAW "
			"rights for this stressor\n");
		return -1;
	}

	fd = open(tun_dev, O_RDWR);
	if (fd < 0) {
		pr_inf("tun stressor will be skipped, cannot open %s\n", tun_dev);
		return -1;

	}
	(void)close(fd);

	return 0;
}

static int stress_set_tun_tap(const char *opt)
{
        bool tun_tap = true;

        (void)opt;
        return stress_set_setting("tun-tap", TYPE_ID_BOOL, &tun_tap);
}

/*
 *  stress_tun
 *	stress tun interface
 */
static int stress_tun(const stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
	const uid_t owner = geteuid();
	const gid_t group = getegid();
	char ip_addr[32];
        bool tun_tap = false;

        (void)stress_get_setting("tun-tap", &tun_tap);

	do {
		int i, fd, sfd, ret, status;
		pid_t pid;
		struct ifreq ifr;
		struct sockaddr_in *tun_addr;
		int port = 2000 + (stress_mwc16() & 0xfff);

		fd = open(tun_dev, O_RDWR);
		if (fd < 0) {
			pr_fail("%s: cannot open %s, errno=%d (%s)\n",
				args->name, tun_dev, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		(void)memset(&ifr, 0, sizeof(ifr));
		ifr.ifr_flags = tun_tap ? IFF_TAP : IFF_TUN;

		ret = ioctl(fd, TUNSETIFF, (void *)&ifr);
		if (ret < 0) {
			pr_fail("%s: ioctl TUNSETIFF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			rc = EXIT_FAILURE;
			break;
		}
#if 0
		pr_inf("%s: using interface %s\n", args->name, ifr.ifr_name);
#endif

		ret = ioctl(fd, TUNSETOWNER, owner);
		if (ret < 0) {
			pr_fail("%s: ioctl TUNSETOWNER failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto clean_up;
		}

		ret = ioctl(fd, TUNSETGROUP, group);
		if (ret < 0) {
			pr_fail("%s: ioctl TUNSETGROUP failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto clean_up;
		}

		sfd = socket(AF_INET, SOCK_DGRAM, 0);
		if (sfd < 0)
			goto clean_up;
		ifr.ifr_addr.sa_family = AF_INET;
		tun_addr = (struct sockaddr_in *)&ifr.ifr_addr;

		/*
		 *  Attempt to assign some kind of random address
		 */
		for (i = 0; i < 32; i++) {
			(void)snprintf(ip_addr, sizeof(ip_addr), "192.168.%" PRIu8 ".%" PRIu8,
				(stress_mwc8() % 252) + 2,
				(stress_mwc8() % 254) + 1);

			(void)inet_pton(AF_INET, ip_addr, &tun_addr->sin_addr);
			ret = ioctl(sfd, SIOCSIFADDR, &ifr);
			if (ret == 0)
				break;
		}
		(void)close(sfd);
		if (ret < 0)
			goto clean_up;

		pid = fork();
		if (pid < 0) {
			goto clean_up;
		} else if (pid == 0) {
			/* Child */

			struct sockaddr_in addr;
			socklen_t len;
			ssize_t n;
			char buffer[4];

			sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (sfd < 0) {
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
					pr_dbg("%s: child socket failed, errno = %d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
				goto child_cleanup_fd;
			}

			(void)memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = port;
			len = sizeof(addr);
			inet_pton(AF_INET, ip_addr, &addr.sin_addr.s_addr);

			ret = bind(sfd, &addr, len);
			if (ret < 0) {
				switch (errno) {
				case EADDRINUSE:
				case ENOMEM:
					rc = EXIT_NO_RESOURCE;
					break;
				case EINTR:
					rc = EXIT_SUCCESS;
					break;
				default:
					pr_dbg("%s: child bind failed, errno = %d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					break;
				}
				goto child_cleanup;
			}

			for (i = 0; i < PACKETS_TO_SEND; i++) {
				n = recvfrom(sfd, buffer, sizeof(buffer), 0,
					(struct sockaddr *)&addr, &len);
				if (n < 0)
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
			char buffer[] = "test";
			ssize_t n;

			ret = ioctl(fd, TUNSETPERSIST, 0);
			if (ret < 0) {
				pr_fail("%s: ioctl TUNSETPERSIST failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				(void)close(fd);
				break;
			}
#if defined(TUNSETNOCSUM)
			ret = ioctl(fd, TUNSETNOCSUM, 1);
			(void)ret;
#endif

#if defined(SIOCGIFHWADDR)
			ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
			(void)ret;
#endif

#if defined(TUNGETVNETHDRSZ)
			{
				int vnet_hdr_sz;

				ret = ioctl(fd, TUNGETVNETHDRSZ, &vnet_hdr_sz);
				if (ret == 0) {
#if defined(TUNSETVNETHDRSZ)
					ret = ioctl(fd, TUNSETVNETHDRSZ, &vnet_hdr_sz);
#endif
				}
				(void)ret;
			}
#endif

#if defined(TUNGETSNDBUF)
			{
				int sndbuf;

				ret = ioctl(fd, TUNGETSNDBUF, &sndbuf);
				if (ret == 0) {
#if defined(TUNSETVNETHDRSZ)
					ret = ioctl(fd, TUNSETSNDBUF, &sndbuf);
#endif
				}
				(void)ret;
			}
#endif

#if defined(TUNGETVNETLE)
			{
				int val;

				ret = ioctl(fd, TUNGETVNETLE, &val);
				if (ret == 0) {
#if defined(TUNSETVNETLE)
					ret = ioctl(fd, TUNSETVNETLE, &val);
#endif
				}
			}
#endif

#if defined(TUNGETVNETBE)
			{
				int val;

				ret = ioctl(fd, TUNGETVNETBE, &val);
				if (ret == 0) {
#if defined(TUNSETVNETBE)
					ret = ioctl(fd, TUNSETVNETBE, &val);
#endif
				}
			}
#endif

#if defined(TUNGETDEVNETNS)
			ret = ioctl(fd, TUNGETDEVNETNS, NULL /* not required */);
			(void)ret;
#endif

			sfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
			if (sfd < 0) {
				pr_fail("%s: parent socket failed, errno = %d (%s)\n",
					args->name, errno, strerror(errno));
				goto child_reap;
			}

			(void)memset(&addr, 0, sizeof(addr));
			addr.sin_family = AF_INET;
			addr.sin_port = port;
			len = sizeof(addr);
			inet_pton(AF_INET, ip_addr, &addr.sin_addr.s_addr);

			for (i = 0; keep_stressing() && (i < PACKETS_TO_SEND); i++) {
				n = sendto(sfd, buffer, sizeof(buffer), 0,
					(struct sockaddr *)&addr, len);
				if (n < 0)
					break;
				shim_sched_yield();
			}
			(void)close(sfd);
		}
child_reap:
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
		if (WEXITSTATUS(status) == EXIT_FAILURE)
			pr_fail("%s: child reading process failed\n", args->name);

clean_up:
		(void)close(fd);
		inc_counter(args);
	} while (keep_stressing());

	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_tun_tap,		stress_set_tun_tap },
        { 0,                    NULL }
};

stressor_info_t stress_tun_info = {
	.stressor = stress_tun,
	.class = CLASS_NETWORK | CLASS_OS,
	.supported = stress_tun_supported,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_tun_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#endif
