/*
 * Copyright (C) 2017-2019 Canonical, Ltd.
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

#define NLA_DATA(na)            ((void *)((char*)(na) + NLA_HDRLEN))
#define NLA_PAYLOAD(len)        ((len) - NLA_HDRLEN)

#define GENL_MSG_DATA(glh)       ((void *)(NLMSG_DATA(glh) + GENL_HDRLEN))
#define GENL_MSG_PAYLOAD(glh)    (NLMSG_PAYLOAD(glh, 0) - GENL_HDRLEN)

/*
 *  netlink message with 1K payload
 */
typedef struct {
	struct nlmsghdr n;
	struct genlmsghdr g;
	char data[1024];
} nlmsg_t;


/*
 *  stress_netlink_task_supported()
 *	check if we can run this as root
 */
static int stress_netlink_task_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_NET_ADMIN)) {
		pr_inf("netlink-task stressor will be skipped, "
			"need to be running with CAP_NET_ADMIN "
			"rights for this stressor\n");
		return -1;
	}
	return 0;
}

/*
 *  stress_netlink_sendcmd()
 *	send a netlink cmd
 */
static int stress_netlink_sendcmd(
	const args_t *args,
	const int sock,
	const uint16_t nlmsg_type,
	const uint16_t nlmsg_pid,
	const uint16_t cmd,
	const uint16_t nla_type,
	const void *nla_data,
	const int nla_len)
{
	struct nlattr *na;
	char *nlmsgbuf;
	ssize_t nlmsgbuf_len;
	struct sockaddr_nl addr;
	nlmsg_t nlmsg;

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
	(void)memcpy(NLA_DATA(na), nla_data, nla_len);
	nlmsg.n.nlmsg_len += NLMSG_ALIGN(na->nla_len);

	nlmsgbuf = (char *)&nlmsg;
	nlmsgbuf_len = nlmsg.n.nlmsg_len;

	(void)memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;

	while (nlmsgbuf_len > 0) {
		ssize_t len;

		len = sendto(sock, nlmsgbuf, nlmsgbuf_len, 0,
			(struct sockaddr *)&addr, sizeof(addr));
		if ((len < 0) &&
		    (errno != EAGAIN) &&
		    (errno != EINTR)) {
			pr_fail("%s: sendto failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			return -1;
		}
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
static void stress_parse_payload(
	const args_t *args,
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
			if (task_pid != pid) {
				pr_fail("%s: TASKSTATS_TYPE_PID got pid %d, "
					"expected %d\n",
					args->name, task_pid, pid);
			}
			break;
		case TASKSTATS_TYPE_STATS:
			t = (struct taskstats *)NLA_DATA(na);
			if ((uint64_t)t->nivcsw < *nivcsw) {
				pr_fail("%s: TASKSTATS_TYPE_STATS got %"
					PRIu64 " involuntary context switches, "
					"expected at least %" PRIu64 "\n",
					args->name, (uint64_t)t->nivcsw, *nivcsw);
			}
			*nivcsw = (uint64_t)t->nivcsw;
			break;
		}
		len += new_len;
		na = (struct nlattr *)((char *)na + new_len);
	}
}

/*
 *   stress_netlink_taskstats_monitor()
 *	monitor parent's activity using taskstats info
 */
static int stress_netlink_taskstats_monitor(
	const args_t *args,
	const int sock,
	const pid_t pid,
	const uint16_t id,
	uint64_t *nivcsw)
{
	do {
		nlmsg_t msg;
		ssize_t msg_len, len;
		int ret;
		pid_t pid_data = pid;
		struct nlattr *na;

		ret = stress_netlink_sendcmd(args, sock, id, pid, TASKSTATS_CMD_GET,
			TASKSTATS_CMD_ATTR_PID, &pid_data, sizeof(pid_data));
		if (ret < 0) {
			pr_err("%s: sendto TASKSTATS_CMD_GET failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		(void)memset(&msg, 0, sizeof(msg));
		msg_len = recv(sock, &msg, sizeof(msg), 0);
		if (msg_len < 0)
			continue;

		if (!NLMSG_OK((&msg.n), msg_len)) {
			pr_fail("%s: recv failed: %d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		msg_len = GENL_MSG_PAYLOAD(&msg.n);
		na = (struct nlattr *)GENL_MSG_DATA(&msg);

		for (len = 0; len < msg_len; len += NLA_ALIGN(na->nla_len)) {
			if (na->nla_type == TASKSTATS_TYPE_AGGR_PID)
				stress_parse_payload(args, na, pid, nivcsw);
		}
		inc_counter(args);
	} while (keep_stressing());

	return 0;
}

/*
 *  stress_netlink_task()
 *	stress netlink task events
 */
static int stress_netlink_task(const args_t *args)
{
	int ret, sock = -1;
	ssize_t len;
	struct sockaddr_nl addr;
	struct nlattr *na;
	nlmsg_t nlmsg;
	const pid_t pid = getpid();
	uint16_t id;
	uint64_t nivcsw = 0ULL;	/* number of involuntary context switches */
	static char name[] = TASKSTATS_GENL_NAME;

	if ((sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_GENERIC)) < 0) {
		if (errno == EPROTONOSUPPORT) {
			pr_err("%s: kernel does not support netlink, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: socket failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)memset(&addr, 0, sizeof(addr));
	addr.nl_family = AF_NETLINK;

	if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		pr_err("%s: bind failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}

	ret = stress_netlink_sendcmd(args, sock, GENL_ID_CTRL, pid, CTRL_CMD_GETFAMILY,
		CTRL_ATTR_FAMILY_NAME, (void *)name, sizeof(name));
	if (ret < 0) {
		pr_err("%s: sendto CTRL_CMD_GETFAMILY failed: %d (%s)\n",
			args->name, errno, strerror(errno));
	}
	len = recv(sock, &nlmsg, sizeof(nlmsg), 0);
	if (len < 0) {
		pr_err("%s: recv failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(sock);
		return EXIT_FAILURE;
	}
	if (!NLMSG_OK((&nlmsg.n), len)) {
		pr_err("%s: recv NLMSG error\n",
			args->name);
		(void)close(sock);
		return EXIT_FAILURE;
	}
	na = (struct nlattr *)GENL_MSG_DATA(&nlmsg);
	na = (struct nlattr *)((char *) na + NLA_ALIGN(na->nla_len));
	if (na->nla_type == CTRL_ATTR_FAMILY_ID) {
		uint16_t *id_ptr = (uint16_t *)NLA_DATA(na);

		id = *id_ptr;
	} else {
		pr_err("%s: failed to get family id\n", args->name);
		(void)close(sock);
		return EXIT_FAILURE;
	}
	do {
		if (stress_netlink_taskstats_monitor(args, sock, pid, id, &nivcsw) < 0)
			break;
	} while (keep_stressing());

#if 0
	/* Some statistics */
	pr_inf("%s: process %d has %" PRIu64 " involuntary context switches\n",
		args->name, pid, nivcsw);
#endif
	(void)close(sock);

	return EXIT_SUCCESS;
}

stressor_info_t stress_netlink_task_info = {
	.stressor = stress_netlink_task,
	.supported = stress_netlink_task_supported,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_netlink_task_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
