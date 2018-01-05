/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#if defined(__linux__)

#include <netinet/in.h>
#include <arpa/inet.h>
#if defined(AF_INET6)
#include <netinet/in.h>
#endif
#if defined(AF_UNIX)
#include <sys/un.h>
#endif
#include <sys/ioctl.h>
#include <net/if.h>

/*
 *  stress_netdev_check()
 *	helper to perform netdevice ioctl and check for failure
 */
static void stress_netdev_check(
	const args_t *args,
	struct ifreq *ifr,
	const int fd,
	const int cmd,
	const char *cmd_name)
{
	if (ioctl(fd, cmd, ifr) < 0) {
		if ((errno != ENOTTY) && (errno != EADDRNOTAVAIL))
			pr_fail("%s: interface '%s' ioctl %s failed, errno=%d (%s)\n",
				args->name, ifr->ifr_name, cmd_name,
				errno, strerror(errno));
	}
}

/*
 *  stress_netdev
 *	stress by heavy socket I/O
 */
int stress_netdev(const args_t *args)
{
	int fd, rc = EXIT_SUCCESS;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		pr_fail_dbg("socket");
		/* failed, kick parent to finish */
		return EXIT_NO_RESOURCE;
	}

	do {
		int i, n;
		struct ifconf ifc;

		/* Get list of transport layer addresses */
		memset(&ifc, 0, sizeof(ifc));
		rc = ioctl(fd, SIOCGIFCONF, &ifc);
		if (rc < 0) {
			pr_fail("%s: ioctl SIOCGIFCONF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/* Do we have any? We should normally have at least lo */
		n = ifc.ifc_len / sizeof(struct ifreq);
		if (!n) {
			pr_dbg("%s: no network interfaces found, skipping.\n",
				args->name);
			break;
		}

		/* Allocate buffer for the addresses */
		ifc.ifc_buf = malloc(ifc.ifc_len);
		if (!ifc.ifc_buf) {
			pr_fail("%s: out of memory allocating interface buffer\n",
				args->name);
			rc = EXIT_NO_RESOURCE;
		}

		/* Fetch the addresses */
		if (ioctl(fd, SIOCGIFCONF, &ifc) < 0) {
			pr_fail("%s: ioctl SIOCGIFCONF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/* And get info on each network device */
		for (i = 0; i < n; i++) {
			struct ifreq *ifr = &ifc.ifc_req[i];

			/* We got the name, check it's index */
			if (ioctl(fd, SIOCGIFINDEX, ifr) < 0)
				continue;

			ifr->ifr_ifindex = i;
			/* Get name */
			if (ioctl(fd, SIOCGIFNAME, ifr) < 0)
				continue;

			/* Check index is sane */
			if (ifr->ifr_ifindex != i) {
				pr_fail("%s: interface '%s' returned "
					"index %d, expected %d\n",
					args->name, ifr->ifr_name,
					ifr->ifr_ifindex, i);
			}

			/* Get flags */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFFLAGS, "SIOCGIFFLAGS");

			/* Get extended flags */
			/*
			stress_netdev_check(args, ifr, fd,
				SIOCGIFPFLAGS, "SIOCGIFPFLAGS");
			*/

			/* Get address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFADDR, "SIOCGIFADDR");

			/* Get netmask */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFNETMASK, "SIOCGIFNETMASK");

			/* Get metric (currently not supported) */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMETRIC, "SIOCGIFMETRIC");

			/* Get the MTU */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMTU, "SIOCGIFMTU");

			/* Get the hardware address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFHWADDR, "SIOCGIFHWADDR");

			/* Get the hardware parameters */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMAP, "SIOCGIFMAP");

			/* Get the transmit queue length */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFTXQLEN, "SIOCGIFTXQLEN");
		}
		free(ifc.ifc_buf);
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);

	return rc;
}

#else
int stress_netdev(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
