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
	{ NULL,	"netdev N",	"start N workers exercising netdevice ioctls" },
	{ NULL,	"netdev-ops N",	"stop netdev workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    defined(SIOCGIFCONF)

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
		if ((errno != ENOTTY) &&
		    (errno != EINVAL) &&
		    (errno != EADDRNOTAVAIL))
			pr_fail("%s: interface '%s' ioctl %s failed, errno=%d (%s)\n",
				args->name, ifr->ifr_name, cmd_name,
				errno, strerror(errno));
	}
}

/*
 *  stress_netdev
 *	stress by heavy socket I/O
 */
static int stress_netdev(const args_t *args)
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
		(void)memset(&ifc, 0, sizeof(ifc));
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

#if defined(SIOCGIFINDEX)
			/* We got the name, check it's index */
			if (ioctl(fd, SIOCGIFINDEX, ifr) < 0)
				continue;
#endif

#if defined(SIOCGIFNAME)
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
#endif

#if defined(SIOCGIFFLAGS)
			/* Get flags */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFFLAGS, "SIOCGIFFLAGS");
#endif

#if defined(SIOCGIFPFLAGS)
			/* Get extended flags */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFPFLAGS, "SIOCGIFPFLAGS");
#endif

#if defined(stress_netdev_check)
			/* Get address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFADDR, "SIOCGIFADDR");
#endif

#if defined(SIOCGIFNETMASK)
			/* Get netmask */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFNETMASK, "SIOCGIFNETMASK");
#endif

#if defined(SIOCGIFNETMASK)
			/* Get metric (currently not supported) */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMETRIC, "SIOCGIFMETRIC");
#endif

#if defined(SIOCGIFNETMASK)
			/* Get the MTU */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMTU, "SIOCGIFMTU");
#endif

#if defined(SIOCGIFNETMASK)
			/* Get the hardware address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFHWADDR, "SIOCGIFHWADDR");
#endif

#if defined(SIOCGIFMAP)
			/* Get the hardware parameters */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFMAP, "SIOCGIFMAP");
#endif

#if defined(SIOCGIFTXQLEN)
			/* Get the transmit queue length */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFTXQLEN, "SIOCGIFTXQLEN");
#endif

#if defined(SIOCGIFDSTADDR)
			/* Get the destination address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFDSTADDR, "SIOCGIFDSTADDR");
#endif

#if defined(SIOCGIFBRDADDR)
			/* Get the broadcast address */
			stress_netdev_check(args, ifr, fd,
				SIOCGIFBRDADDR, "SIOCGIFBRDADDR");
#endif
		}
		free(ifc.ifc_buf);
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);

	return rc;
}

stressor_info_t stress_netdev_info = {
	.stressor = stress_netdev,
	.class = CLASS_NETWORK,
	.help = help
};
#else
stressor_info_t stress_netdev_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK,
	.help = help
};
#endif
