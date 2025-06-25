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

#if defined(HAVE_LINUX_TASKSTATS_H)
#include <linux/taskstats.h>
#endif

#if defined(HAVE_LINUX_GENETLINK_H)
#include <linux/genetlink.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"netlink-task N",	"start N workers exercising netlink tasks events" },
	{ NULL,	"netlink-task-ops N",	"stop netlink-task workers after N bogo events" },
	{ NULL,	NULL,			NULL }
};

#if defined (__linux__) && 		\
    defined(HAVE_LINUX_CONNECTOR_H) &&	\
    defined(HAVE_LINUX_NETLINK_H) &&	\
    defined(HAVE_LINUX_CN_PROC_H) &&	\
    defined(HAVE_LINUX_GENETLINK_H) &&	\
    defined(HAVE_LINUX_TASKSTATS_H)

#define NLA_DATA(na)            ((void *)((char *)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)        ((len) - NLA_HDRLEN)

#define GENL_MSG_DATA(glh)       ((void *)((char *)NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENL_MSG_PAYLOAD(glh)    (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)

/*
 *  netlink message with 1K payload
 */
typedef struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char data[1024];	/* cppcheck-suppress unusedStructMember */
} stress_nlmsg_t;


/*
 *  stress_netlink_task_supported()
 *	check if we can run this with SHIM_CAP_NET_ADMIN capability
 */
static int stress_netlink_task_supported(const char *name)
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
 *  stress_netlink_sendcmd()
 *	send a netlink cmd
 */
static int OPTIMIZE3 stress_netlink_sendcmd(
	stress_args_t *args,
	const int sock,
	const uint16_t nlmsg_type,
	const uint32_t nlmsg_pid,
	const uint8_t cmd,
	const uint16_t nla_type,
	const void *nla_data,
	const uint16_t nla_len)
{
	struct nlattr *na;
	char *nlmsgbuf;
	ssize_t nlmsgbuf_len;
	struct sockaddr_nl addr;
	stress_nlmsg_t nlmsg ALIGN64;

	nlmsg.n.nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN);
	nlmsg.n.nlmsg_type = nlmsg_type;
	nlmsg.n.nlmsg_flags = NLM_F_REQUEST;
	nlmsg.n.nlmsg_pid = nlmsg_pid;
	nlmsg.n.nlmsg_seq = 0;
	nlmsg.g.cmd = cmd;
	nlmsg.g.version = 0x1;

	na = (struct nlattr *)GENL_MSG_DATA(&nlmsg);
	na->nla_type = nla_type;
	na->nla_len = nla_len + NLA_HDRLEN;
	(void)shim_memcpy(NLA_DATA(na), nla_data, (size_t)nla_len);
	nlmsg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	nlmsgbuf = (char *)&nlmsg;
	nlmsgbuf_len = nlmsg.n.nlmsg_len;

	(void)shim_memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;

	while (nlmsgbuf_len > 0) {
		register ssize_t len;

		/* Keep static analysis tools happy */
		if (UNLIKELY(nlmsgbuf_len > (ssize_t)sizeof(stress_nlmsg_t)))
			break;

		len = sendto(sock, nlmsgbuf, (size_t)nlmsgbuf_len, 0,
			(struct sockaddr *)&addr, sizeof(addr));
		if (UNLIKELY(len < 0)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				return 0;
			pr_fail("%s: sendto failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
		/* avoid underflowing nlmsgbuf_len on subtraction */
		if (UNLIKELY(len > nlmsgbuf_len))
			break;
		nlmsgbuf_len -= len;
		nlmsgbuf += len;
	}
	return 0;
}

/*
 *  stress_parse_payload()
 *	parse the aggregated message payload and sanity
 * 	check that the pid matches and involuntary context
 *	switches are incrementing over time.
 */
static void OPTIMIZE3 stress_parse_payload(
	stress_args_t *args,
	struct nlattr *na,
	const pid_t pid,
	uint64_t *nivcsw)
{
	const ssize_t total_len = NLA_PAYLOAD(na->nla_len);
	ssize_t len;

	na = (struct nlattr *)NLA_DATA(na);
	for (len = 0; len < total_len; ) {
		const ssize_t new_len = NLA_ALIGN(na->nla_len);
		struct taskstats *t;
		pid_t task_pid;

		switch (na->nla_type) {
		case TASKSTATS_TYPE_PID:
			task_pid = *(pid_t *)NLA_DATA(na);
			if (UNLIKELY(task_pid != pid)) {
				pr_fail("%s: TASKSTATS_TYPE_PID got PID %" PRIdMAX ", "
					"expected %" PRIdMAX "\n",
					args->name,
					(intmax_t)task_pid, (intmax_t)pid);
			}
			break;
		case TASKSTATS_TYPE_STATS:
			t = (struct taskstats *)NLA_DATA(na);
			if (UNLIKELY((uint64_t)t->nivcsw < *nivcsw)) {
				pr_fail("%s: TASKSTATS_TYPE_STATS got %"
					PRIu64 " involuntary context switches, "
					"expected at least %" PRIu64 "\n",
					args->name, (uint64_t)t->nivcsw, *nivcsw);
			}
			*nivcsw = (uint64_t)t->nivcsw;
			break;
		}
		len += new_len;
		na = (struct nlattr *)(uintptr_t)((char *)na + new_len);
	}
}

/*
 *   stress_netlink_taskstats_monitor()
 *	monitor parent's activity using taskstats info
 */
static int OPTIMIZE3 stress_netlink_taskstats_monitor(
	stress_args_t *args,
	const int sock,
	const pid_t pid,
	const uint16_t id,
	uint64_t *nivcsw)
{
	do {
		stress_nlmsg_t msg ALIGN64;
		ssize_t msg_len, len;
		int ret;
		pid_t pid_data = pid;
		struct nlattr *na;

		ret = stress_netlink_sendcmd(args, sock, id, (uint32_t)pid,
						TASKSTATS_CMD_GET,
						TASKSTATS_CMD_ATTR_PID,
						&pid_data,
						(uint16_t)sizeof(pid_data));
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sendto TASKSTATS_CMD_GET failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		(void)shim_memset(&msg, 0, sizeof(msg));
		msg_len = recv(sock, &msg, sizeof(msg), 0);
		if (UNLIKELY(msg_len < 0))
			continue;

		if (!NLMSG_OK((&msg.n), (unsigned int)msg_len)) {
			pr_fail("%s: recv failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		msg_len = GENL_MSG_PAYLOAD(&msg.n);
		na = (struct nlattr *)GENL_MSG_DATA(&msg);

		for (len = 0; len < msg_len; len += NLA_ALIGN(na->nla_len)) {
			if (na->nla_type == TASKSTATS_TYPE_AGGR_PID)
				stress_parse_payload(args, na, pid, nivcsw);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return 0;
}

/*
 *  stress_netlink_task()
 *	stress netlink task events
 */
static int stress_netlink_task(stress_args_t *args)
{
	int ret, sock = -1;
	ssize_t len;
	struct sockaddr_nl addr;
	struct nlattr *na;
	stress_nlmsg_t nlmsg ALIGN64;
	const pid_t pid = getpid();
	uint16_t id;
	uint64_t nivcsw = 0ULL;	/* number of involuntary context switches */
	static const char name[] = TASKSTATS_GENL_NAME;

	if ((sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC)) < 0) {
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
	addr.nl_family = AF_NETLINK;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		pr_err("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	ret = stress_netlink_sendcmd(args, sock, GENL_ID_CTRL, (uint32_t)pid,
					CTRL_CMD_GETFAMILY,
					CTRL_ATTR_FAMILY_NAME,
					(const void *)name, sizeof(name));
	if (ret < 0) {
		pr_fail("%s: sendto CTRL_CMD_GETFAMILY failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
	}
	len = recv(sock, &nlmsg, sizeof(nlmsg), 0);
	if (len < 0) {
		pr_fail("%s: recv failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}
	if (!NLMSG_OK((&nlmsg.n), (unsigned int)len)) {
		pr_fail("%s: recv NLMSG error\n",
			args->name);
		(void)close(sock);
		return EXIT_FAILURE;
	}
	na = (struct nlattr *)(uintptr_t)GENL_MSG_DATA(&nlmsg);
	na = (struct nlattr *)(uintptr_t)((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
		const uint16_t *id_ptr = (uint16_t *)NLA_DATA(na);

		id = *id_ptr;
	} else {
		pr_fail("%s: failed to get family id\n", args->name);
		(void)close(sock);
		return EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(stress_netlink_taskstats_monitor(args, sock, pid, id, &nivcsw) < 0))
			break;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if 0
	/* Some statistics */
	pr_inf("%s: process %d has %" PRIu64 " involuntary context switches\n",
		args->name, pid, nivcsw);
#endif
	(void)close(sock);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_netlink_task_info = {
	.stressor = stress_netlink_task,
	.supported = stress_netlink_task_supported,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_netlink_task_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without linux/connector.h, linux/netlink.h, linux/cn_proc.h, linux/taskstats.h or linux/genetlink.h support"
};
#endif
