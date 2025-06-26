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

#include <sys/ioctl.h>

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
	stress_args_t *args,
	struct ifreq *ifr,
	const int fd,
	const unsigned long int cmd,
	const char *cmd_name,
	int *rc)
{
	if (UNLIKELY(ioctl(fd, cmd, ifr) < 0)) {
		if ((errno != ENOTTY) &&
		    (errno != EINVAL) &&
		    (errno != EADDRNOTAVAIL) &&
		    (errno != EOPNOTSUPP) &&
		    (errno != EBUSY) &&
		    (errno != EPERM)) {
			pr_fail("%s: interface '%s' ioctl %s failed, errno=%d (%s)\n",
				args->name, ifr->ifr_name, cmd_name,
				errno, strerror(errno));
			*rc = EXIT_FAILURE;
		}
	}
}

#define STRESS_NETDEV_CHECK(args, ifr, fd, cmd, rc)	\
	stress_netdev_check(args, ifr, fd, cmd, #cmd, rc)

/*
 *  stress_netdev
 *	stress netdev
 */
static int stress_netdev(stress_args_t *args)
{
	int fd, rc = EXIT_SUCCESS;

	if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		/* failed, kick parent to finish */
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i, n;
		struct ifconf ifc;

		/* Get list of transport layer addresses */
		(void)shim_memset(&ifc, 0, sizeof(ifc));
		rc = ioctl(fd, SIOCGIFCONF, &ifc);
		if (UNLIKELY(rc < 0)) {
			pr_fail("%s: ioctl SIOCGIFCONF failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			break;
		}

		/* Do we have any? We should normally have at least lo */
		n = ifc.ifc_len / (int)sizeof(struct ifreq);
		if (UNLIKELY(!n)) {
			if (stress_instance_zero(args))
				pr_dbg_skip("%s: no network interfaces found, skipping.\n",
					args->name);
			break;
		}

		/* Allocate buffer for the addresses */
		ifc.ifc_buf = (char *)malloc((size_t)ifc.ifc_len);
		if (UNLIKELY(!ifc.ifc_buf)) {
			pr_fail("%s: failed to allocated %zu byte interface buffer%s\n",
				args->name, (size_t)ifc.ifc_len,
				stress_get_memfree_str());
			rc = EXIT_NO_RESOURCE;
		}

		/* Fetch the addresses */
		if (UNLIKELY(ioctl(fd, SIOCGIFCONF, &ifc) < 0)) {
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
			if (UNLIKELY(ioctl(fd, SIOCGIFINDEX, ifr) < 0))
				continue;
#endif

#if defined(SIOCGIFNAME)
			ifr->ifr_ifindex = i;
			/* Get name */
			if (UNLIKELY(ioctl(fd, SIOCGIFNAME, ifr) < 0))
				continue;

			/* Check index is sane */
			if (UNLIKELY(ifr->ifr_ifindex != i)) {
				pr_fail("%s: interface '%s' returned index %d, expected %d\n",
					args->name, ifr->ifr_name,
					ifr->ifr_ifindex, i);
			}
#endif

#if defined(SIOCGIFFLAGS)
			/* Get flags */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFFLAGS, &rc);
#endif

#if defined(SIOCGIFPFLAGS)
			/* Get extended flags */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFPFLAGS, &rc);
#endif

#if defined(SIOCGIFADDR)
			/* Get address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFADDR, &rc);
#endif

#if defined(SIOCGIFNETMASK)
			/* Get netmask */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFNETMASK, &rc);
#endif

#if defined(SIOCGIFMETRIC)
			/* Get metric (currently not supported) */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMETRIC, &rc);
#endif

#if defined(SIOCGIFMTU)
			/* Get the MTU */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMTU, &rc);
#endif

#if defined(SIOCGIFHWADDR)
			/* Get the hardware address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFHWADDR, &rc);
#endif

#if defined(SIOCGIFMAP)
			/* Get the hardware parameters */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMAP, &rc);
#endif

#if defined(SIOCGIFTXQLEN)
			/* Get the transmit queue length */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFTXQLEN, &rc);
#endif

#if defined(SIOCGIFDSTADDR)
			/* Get the destination address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFDSTADDR, &rc);
#endif

#if defined(SIOCGIFBRDADDR)
			/* Get the broadcast address */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFBRDADDR, &rc);
#endif
#if defined(SIOCGMIIPHY) && 0
			/* Get from current PHY, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGMIIPHY, &rc);
#endif
#if defined(SIOCGMIIREG) && 0
			/* Get reg, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGMIIREG, &rc);
#endif
#if defined(SIOCSIFFLAGS) && 0
			/* Get flags, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCSIFFLAGS, &rc);
#endif
#if defined(SIOCSIFMETRIC) && 0
			/* Get metric, disabled for now */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCSIFMETRIC, &rc);
#endif
#if defined(SIOCGIFMEM)
			/* Get memory space, not implemented */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFMEM, &rc);
#endif
#if defined(SIOCGIFLINK)
			/* Get if link, not implemented */
			STRESS_NETDEV_CHECK(args, ifr, fd, SIOCGIFLINK, &rc);
#endif
#if defined(SIOCGIFNAME)
			/* Get name with illegal index */
			ifr->ifr_ifindex = (int)stress_mwc32();
			if (ioctl(fd, SIOCGIFNAME, ifr) < 0)
				continue;
#endif
		}

		/* Use invalid ifc_len */
		if (n > 1) {
			ifc.ifc_len = (int)(sizeof(struct ifreq) - 1);
			VOID_RET(int, ioctl(fd, SIOCGIFCONF, &ifc));
		}

		ifc.ifc_len = -1;
		VOID_RET(int, ioctl(fd, SIOCGIFCONF, &ifc));

		free(ifc.ifc_buf);
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(fd);

	return rc;
}

const stressor_info_t stress_netdev_info = {
	.stressor = stress_netdev,
	.classifier = CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_netdev_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/sockios.h, net/if.h, struct ifconf or ioctl() SIOCGIFCONF command support"
};
#endif
