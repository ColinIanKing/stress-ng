/*
 * Copyright (C) 2017 Canonical, Ltd.
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

#include <linux/version.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <linux/cn_proc.h>

#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(2,0,0)
#endif

/*
 *  stress_netlink_proc_supported()
 *	check if we can run this as root
 */
int stress_netlink_proc_supported(void)
{
	if (geteuid() != 0) {
		pr_inf("netlink-proc stressor will be skipped, "
			"need to be running as root for this stressor\n");
		return -1;
	}
	return 0;
}

/*
 *   monitor()
 *	monitor system activity
 */
static int monitor(const args_t *args, const int sock)
{
	struct nlmsghdr *nlmsghdr;

	ssize_t len;
	char __attribute__ ((aligned(NLMSG_ALIGNTO)))buf[4096];

	if ((len = recv(sock, buf, sizeof(buf), 0)) == 0)
		return 0;

	if (len == -1) {
		switch (errno) {
		case EINTR:
		case ENOBUFS:
			return 0;
		default:
			return -1;
		}
	}

	for (nlmsghdr = (struct nlmsghdr *)buf;
		NLMSG_OK (nlmsghdr, len);
		nlmsghdr = NLMSG_NEXT (nlmsghdr, len)) {

		struct cn_msg *cn_msg;
		struct proc_event *proc_ev;

		if (!g_keep_stressing_flag)
			return 0;

		if ((nlmsghdr->nlmsg_type == NLMSG_ERROR) ||
		    (nlmsghdr->nlmsg_type == NLMSG_NOOP))
			continue;

		cn_msg = NLMSG_DATA(nlmsghdr);
		if ((cn_msg->id.idx != CN_IDX_PROC) ||
		    (cn_msg->id.val != CN_VAL_PROC))
			continue;

		proc_ev = (struct proc_event *)cn_msg->data;

		switch (proc_ev->what) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
		case PROC_EVENT_FORK:
		case PROC_EVENT_EXEC:
		case PROC_EVENT_EXIT:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,9,0)
		case PROC_EVENT_COREDUMP:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
		case PROC_EVENT_COMM:
#endif
			inc_counter(args);
			break;
		default:
			break;
		}
	}
	return 0;
}


/*
 *  spawn_several()
 *	create a bunch of processes
 */
static void spawn_several(const char *name, int n, int max)
{
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		char newname[128];

		(void)snprintf(newname, sizeof(newname), "stress-ng-%d", n);
		set_proc_name(newname);

		if (n >= max) {
			set_proc_name("stress-ng-dead");
			_exit(0);
		} else {
			set_proc_name("stress-ng-spawn");
			spawn_several(name, n + 1, max);
		}
	} else if (pid < 0) {
		return;
	} else {
		int status;

		if (n != 0)
			set_proc_name("stress-ng-wait");
		(void)waitpid(pid, &status, 0);
		if (n != 0)
			_exit(0);
	}
}

/*
 *  stress_netlink_proc()
 *	stress netlink proc events
 */
int stress_netlink_proc(const args_t *args)
{
	int sock = -1;
	struct sockaddr_nl addr;
	struct iovec iov[3];
	struct nlmsghdr nlmsghdr;
	struct cn_msg cn_msg;
	enum proc_cn_mcast_op op;

	if ((sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_err("%s: kernel does not support netlink, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: socket failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	memset(&addr, 0, sizeof(addr));
	addr.nl_pid = getpid();
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = CN_IDX_PROC;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		pr_err("%s: bind failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	memset(&nlmsghdr, 0, sizeof(nlmsghdr));
	nlmsghdr.nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(op));
	nlmsghdr.nlmsg_pid = getpid();
	nlmsghdr.nlmsg_type = NLMSG_DONE;
	iov[0].iov_base = &nlmsghdr;
	iov[0].iov_len = sizeof(nlmsghdr);

	memset(&cn_msg, 0, sizeof(cn_msg));
	cn_msg.id.idx = CN_IDX_PROC;
	cn_msg.id.val = CN_VAL_PROC;
	cn_msg.len = sizeof(enum proc_cn_mcast_op);
	iov[1].iov_base = &cn_msg;
	iov[1].iov_len = sizeof(cn_msg);

	op = PROC_CN_MCAST_LISTEN;
	iov[2].iov_base = &op;
	iov[2].iov_len = sizeof(op);

	if (writev(sock, iov, 3) < 0) {
		pr_err("%s: writev failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	do {
		spawn_several(args->name, 0, 5);
		if (monitor(args, sock) < 0)
			break;
	} while (keep_stressing());

	(void)close(sock);

	return EXIT_SUCCESS;
}
#else
/*
 *  stress_netlink_proc_supported()
 *	check if we can run this stressor
 */
int stress_netlink_proc_supported(void)
{
	if (geteuid() != 0) {
		pr_inf("netlink-proc stressor will be skipped, "
			"as it is not supported by this operating system\n");
		return -1;
	}
	return 0;
}

int stress_netlink_proc(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
