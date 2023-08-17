// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#endif

#if defined(HAVE_NET_IF_H)
#include <net/if.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"netdev N",	"start N workers exercising netdevice ioctls" },
	{ NULL,	"netdev-ops N",	"stop netdev workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    defined(SIOCGIFCONF) &&	\
    defined(HAVE_IFCONF) &&	\
    defined(HAVE_IFREQ)

/*
 *  As per man 7 netdevice advise, workaround glibc 2.1 missing
 *  ifr_newname
 */
#ifndef ifr_newname
#define ifr_newname     ifr_ifru.ifru_slave
#endif

/*
 *  stress_netdev_check()
 *	helper to perform netdevice ioctl and check for failure
 */
static void stress_netdev_check(
	const stress_args_t *args,
	struct ifreq *ifr,
	const int fd,
	const unsigned long cmd,
	const char *cmd_name)
{
	if (ioctl(fd, cmd, ifr) < 0) {
		if ((errno != ENOTTY) &&
		    (errno != EINVAL) &&
		    (errno != EADDRNOTAVAIL) &&
		    (errno != EOPNOTSUPP) &&
		    (errno != EBUSY) &&
		    (errno != EPERM))
			pr_fail("%s: interface '%s' ioctl %s failed, errno=%d (%s)\n",
				args->name, ifr->ifr_name, cmd_name,
				errno, strerror(errno));
	}
}

#define STRESS_NETDEV_CHECK(args, ifr, fd, cmd)	\
	stress_netdev_check(args, ifr, fd, cmd, #cmd)

/*
 *  stress_netdev
 *	stress netdev
 */
static int stress_netdev(const stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		/* failed, kick parent to finish */
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i, n;
		struct ifconf ifc;

		/* Get list of transport layer addresses */
		(void)shim_memset(&ifc, 0, sizeof(ifc));
		rc = ioctl(fd, SIOCGIFCONF, &ifc);
		if (rc < 0) {
			pr_fail("%s: ioctl SIOCGIFCONF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/* Do we have any? We should normally have at least lo */
		n = ifc.ifc_len / (int)sizeof(struct ifreq);
		if (!n) {
			if (args->instance == 0)
				pr_dbg_skip("%s: no network interfaces found, skipping.\n",
					args->name);
			break;
		}

		/* Allocate buffer for the addresses */
		ifc.ifc_buf = malloc((size_t)ifc.ifc_len);
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
				pr_fail("%s: interface '%s' returned index %d, expected %d\n",
					args->name, ifr->ifr_name,
					ifr->ifr_ifindex, i);
			}
#endif

#if defined(SIOCGIFFLAGS)
			/* Get flags */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFFLAGS);
#endif

#if defined(SIOCGIFPFLAGS)
			/* Get extended flags */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFPFLAGS);
#endif

#if defined(SIOCGIFADDR)
			/* Get address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFADDR);
#endif

#if defined(SIOCGIFNETMASK)
			/* Get netmask */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFNETMASK);
#endif

#if defined(SIOCGIFMETRIC)
			/* Get metric (currently not supported) */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMETRIC);
#endif

#if defined(SIOCGIFMTU)
			/* Get the MTU */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMTU);
#endif

#if defined(SIOCGIFHWADDR)
			/* Get the hardware address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFHWADDR);
#endif

#if defined(SIOCGIFMAP)
			/* Get the hardware parameters */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMAP);
#endif

#if defined(SIOCGIFTXQLEN)
			/* Get the transmit queue length */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFTXQLEN);
#endif

#if defined(SIOCGIFDSTADDR)
			/* Get the destination address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFDSTADDR);
#endif

#if defined(SIOCGIFBRDADDR)
			/* Get the broadcast address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFBRDADDR);
#endif
#if defined(SIOCGMIIPHY) && 0
			/* Get from current PHY, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGMIIPHY);
#endif
#if defined(SIOCGMIIREG) && 0
			/* Get reg, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGMIIREG);
#endif
#if defined(SIOCSIFFLAGS) && 0
			/* Get flags, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCSIFFLAGS);
#endif
#if defined(SIOCSIFMETRIC) && 0
			/* Get metric, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCSIFMETRIC);
#endif
#if defined(SIOCGIFMEM)
			/* Get memory space, not implemented */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMEM);
#endif
#if defined(SIOCGIFLINK)
			/* Get if link, not implemented */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFLINK);
#endif
		}
		free(ifc.ifc_buf);
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return rc;
}

stressor_info_t stress_netdev_info = {
	.stressor = stress_netdev,
	.class = CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_netdev_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/sockios.h, net/if.h, struct ifconf or ioctl() SIOCGIFCONF command support"
};
#endif
