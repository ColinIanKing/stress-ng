/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
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
#include "core-capabilities.h"
#include <sys/socket.h>

#if defined(HAVE_LINUX_CN_PROC_H)
#include <linux/cn_proc.h>
#endif

#if defined(HAVE_LINUX_CONNECTOR_H)
#include <linux/connector.h>
#endif

#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_LINUX_VERSION_H)
#include <linux/version.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"netlink-proc N",	"start N workers exercising netlink process events" },
	{ NULL,	"netlink-proc-ops N",	"stop netlink-proc workers after N bogo events" },
	{ NULL,	NULL,			NULL }
};

#if defined (__linux__) && 		\
    defined(HAVE_LINUX_CONNECTOR_H) &&	\
    defined(HAVE_LINUX_NETLINK_H) &&	\
    defined(HAVE_LINUX_CN_PROC_H)

#ifndef LINUX_VERSION_CODE
#define LINUX_VERSION_CODE KERNEL_VERSION(2,0,0)
#endif

/*
 *  stress_netlink_proc_supported()
 *	check if we can run this as root
 */
static int stress_netlink_proc_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_NET_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_NET_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *   monitor()
 *	monitor system activity
 */
static int monitor(stress_args_t *args, const int sock)
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
		NLMSG_OK(nlmsghdr, (unsigned int)len);
		nlmsghdr = NLMSG_NEXT(nlmsghdr, len)) {

		const struct cn_msg *cn_msg;
		const struct proc_event *proc_ev;

		if (UNLIKELY(!stress_continue_flag()))
			return 0;

		if ((nlmsghdr->nlmsg_type == NLMSG_ERROR) ||
		    (nlmsghdr->nlmsg_type == NLMSG_NOOP))
			continue;

		cn_msg = NLMSG_DATA(nlmsghdr);
		if ((cn_msg->id.idx != CN_IDX_PROC) ||
		    (cn_msg->id.val != CN_VAL_PROC))
			continue;

		proc_ev = (const struct proc_event *)cn_msg->data;

		switch (proc_ev->what) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
		case PROC_EVENT_NONE:
			break;
		case PROC_EVENT_FORK:
		case PROC_EVENT_EXEC:
		case PROC_EVENT_EXIT:
		case PROC_EVENT_UID:
		case PROC_EVENT_GID:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,31)
		case PROC_EVENT_SID:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,10,0)
		case PROC_EVENT_COREDUMP:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
		case PROC_EVENT_COMM:
#endif
#if LINUX_VERSION_CODE >= KERNEL_VERSION(3,1,0)
		case PROC_EVENT_PTRACE:
#endif
			stress_bogo_inc(args);
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
static void spawn_several(const char *name, const int n, const int max)
{
	pid_t pid;

	pid = fork();
	if (pid == 0) {
		char newname[128];

		(void)snprintf(newname, sizeof(newname), "%d", n);
		stress_set_proc_name(newname);

		if (n >= max) {
			stress_set_proc_name("dead");
			_exit(0);
		} else {
			stress_set_proc_name("spawn");
			spawn_several(name, n + 1, max);
		}
	} else if (pid < 0) {
		return;
	} else {
		int status;

		if (n != 0)
			stress_set_proc_name("wait");
		(void)shim_waitpid(pid, &status, 0);
		if (n != 0)
			_exit(0);
	}
}

/*
 *  stress_netlink_proc()
 *	stress netlink proc events
 */
static int stress_netlink_proc(stress_args_t *args)
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
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.nl_pid = (uint32_t)getpid();
	addr.nl_family = AF_NETLINK;
	addr.nl_groups = CN_IDX_PROC;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		if (errno == EPERM) {
			pr_inf_skip("%s: bind failed, no permission, "
				"skipping stressor\n", args->name);
			(void)close(sock);
			return EXIT_NO_RESOURCE;
		}
		pr_err("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	(void)shim_memset(&nlmsghdr, 0, sizeof(nlmsghdr));
	nlmsghdr.nlmsg_len = NLMSG_LENGTH(sizeof(cn_msg) + sizeof(op));
	nlmsghdr.nlmsg_pid = (uint32_t)getpid();
	nlmsghdr.nlmsg_type = NLMSG_DONE;
	iov[0].iov_base = &nlmsghdr;
	iov[0].iov_len = sizeof(nlmsghdr);

	(void)shim_memset(&cn_msg, 0, sizeof(cn_msg));
	cn_msg.id.idx = CN_IDX_PROC;
	cn_msg.id.val = CN_VAL_PROC;
	cn_msg.len = sizeof(enum proc_cn_mcast_op);
	iov[1].iov_base = &cn_msg;
	iov[1].iov_len = sizeof(cn_msg);

	op = PROC_CN_MCAST_LISTEN;
	iov[2].iov_base = &op;
	iov[2].iov_len = sizeof(op);

	if (writev(sock, iov, 3) < 0) {
		if (errno == ECONNREFUSED) {
			pr_inf_skip("%s: net link write failed, errno=%d (%s), skipping stressor\n",
				args->name, errno, strerror(errno));
			(void)close(sock);
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: writev failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		spawn_several(args->name, 0, 5);
		if (monitor(args, sock) < 0)
			break;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)close(sock);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_netlink_proc_info = {
	.stressor = stress_netlink_proc,
	.supported = stress_netlink_proc_supported,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
const stressor_info_t stress_netlink_proc_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without linux/connector.h, linux/netlink.h or linux/cn_proc.h support"
};
#endif
