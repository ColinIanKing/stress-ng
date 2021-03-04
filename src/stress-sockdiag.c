/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"sockdiag N",	  "start N workers exercising sockdiag netlink" },
	{ NULL,	"sockdiag-ops N", "stop sockdiag workers after N bogo messages" },
	{ NULL,	NULL,		  NULL }
};

#if defined(__linux__) && 		\
    defined(HAVE_LINUX_SOCK_DIAG_H) &&	\
    defined(HAVE_LINUX_NETLINK_H) && 	\
    defined(HAVE_LINUX_RTNETLINK_H) &&	\
    defined(HAVE_LINUX_UNIX_DIAG_H)

typedef struct {
	struct nlmsghdr nlh;
	struct unix_diag_req udr;
} stress_sockdiag_request_t;

static int sockdiag_send(const stress_args_t *args, const int fd)
{
	static size_t family = 0;

	static struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};

	static stress_sockdiag_request_t request = {
		.nlh = {
			.nlmsg_len = sizeof(stress_sockdiag_request_t),
			.nlmsg_type = SOCK_DIAG_BY_FAMILY,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP
		},
		.udr = {
			.sdiag_family = AF_UNIX,
			.sdiag_protocol = 0,
			.udiag_states = -1,
			.udiag_ino = 0,
			.udiag_cookie = { 0 },
			.udiag_show = 0,
		}
	};

	static struct iovec iov = {
		.iov_base = &request,
		.iov_len = sizeof(request)
	};

	static struct msghdr msg = {
		.msg_name = (void *)&nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1
	};

	static const int families[] = {
#if defined(AF_UNIX)
		AF_UNIX,
#endif
#if defined(AF_LOCAL)
		AF_LOCAL,
#endif
#if defined(AF_INET)
		AF_INET,
#endif
#if defined(AF_AX25)
		AF_AX25,
#endif
#if defined(AF_IPX)
		AF_IPX,
#endif
#if defined(AF_APPLETALK)
		AF_APPLETALK,
#endif
#if defined(AF_X25)
		AF_X25,
#endif
#if defined(AF_INET6)
		AF_INET6,
#endif
#if defined(AF_DECnet)
		AF_DECnet,
#endif
#if defined(AF_KEY)
		AF_KEY,
#endif
#if defined(AF_NETLINK)
		AF_NETLINK,
#endif
#if defined(AF_PACKET)
		AF_PACKET,
#endif
#if defined(AF_RDS)
		AF_RDS,
#endif
#if defined(AF_PPPOX)
		AF_PPPOX,
#endif
#if defined(AF_LLC)
		AF_LLC,
#endif
#if defined(AF_IB)
		AF_IB,
#endif
#if defined(AF_MPLS)
		AF_MPLS,
#endif
#if defined(AF_CAN)
		AF_CAN,
#endif
#if defined(AF_TIPC)
		AF_TIPC,
#endif
#if defined(AF_BLUETOOTH)
		AF_BLUETOOTH,
#endif
#if defined(AF_ALG)
		AF_ALG,
#endif
#if defined(AF_VSOCK)
		AF_VSOCK,
#endif
#if defined(AF_KCM)
		AF_KCM,
#endif
#if defined(AF_XDP)
		AF_XDP
#endif
	};

	while (keep_stressing(args)) {
		ssize_t ret;
		unsigned int i;

		request.udr.sdiag_family = families[family];

		for (i = 0; i < 32; i++) {
			request.udr.udiag_show = 1U << i;
			ret = sendmsg(fd, &msg, 0);
			if (ret > 0)
				return 1;
			if (errno != EINTR)
				return -1;
		}
		request.udr.udiag_show = ~0;
		ret = sendmsg(fd, &msg, 0);
		if (ret > 0)
			return 1;
		if (errno != EINTR)
			return -1;

		family++;
		if (family >= SIZEOF_ARRAY(families))
			family = 0;
	}

	return 0;
}

static int stress_sockdiag_parse(
	const stress_args_t *args,
	struct unix_diag_msg *diag,
	unsigned int len)
{
	struct rtattr *attr;
	unsigned int rta_len;

	if (len < NLMSG_LENGTH(sizeof(*diag))) {
		/* short response, ignore for now */
		return 0;
	}

	rta_len = len - NLMSG_LENGTH(sizeof(*diag));
	for (attr = (struct rtattr *) (diag + 1);
	     RTA_OK(attr, rta_len) && keep_stressing(args);
	     attr = RTA_NEXT(attr, rta_len)) {
		inc_counter(args);
	}

	return 0;
}

static int sockdiag_recv(const stress_args_t *args, const int fd)
{
	static uint32_t buf[4096];
	int flags = 0;

	static struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};

	static struct iovec iov = {
		.iov_base = buf,
		.iov_len = sizeof(buf)
	};

	for (;;) {
		ssize_t ret;
		static struct msghdr msg = {
			.msg_name = (void *)&nladdr,
			.msg_namelen = sizeof(nladdr),
			.msg_iov = &iov,
			.msg_iovlen = 1
		};
		struct nlmsghdr *h = (struct nlmsghdr *)buf;

		ret = recvmsg(fd, &msg, flags);
		if (ret == 0)
			break;
		if (ret < 0) {
			if (errno == EINTR)
				continue;

			return -1;
		}
		if (!NLMSG_OK(h, ret))
			return -1;

		for (; NLMSG_OK(h, ret); h = NLMSG_NEXT(h, ret)) {
			if (h->nlmsg_type == NLMSG_DONE)
				return 0;

			if (h->nlmsg_type == NLMSG_ERROR)
				return -1;

			if (h->nlmsg_type != SOCK_DIAG_BY_FAMILY)
				return -1;

			if (stress_sockdiag_parse(args, NLMSG_DATA(h), h->nlmsg_len))
				return -1;
		}
	}
	return 0;
}

/*
 *  stress_sockdiag
 *	stress by heavy socket I/O
 */
static int stress_sockdiag(const stress_args_t *args)
{
	int ret = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int fd, rc;

		fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_SOCK_DIAG);
		if (fd < 0) {
			if (errno == EPROTONOSUPPORT) {
				pr_inf("%s: NETLINK_SOCK_DIAG not supported, skipping stressor\n",
					args->name);
				ret = EXIT_NOT_IMPLEMENTED;
				break;
			}
			pr_err("%s: NETLINK_SOCK_DIAG open failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			break;
		}
		rc = sockdiag_send(args, fd);
		if (rc < 0) {
			pr_err("%s: NETLINK_SOCK_DIAG send query failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			ret = EXIT_FAILURE;
			(void)close(fd);
			break;
		} else if (rc == 0) {
			/* Nothing sent or timed out? */
			(void)close(fd);
			break;
		}
		(void)sockdiag_recv(args, fd);
		(void)close(fd);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

stressor_info_t stress_sockdiag_info = {
	.stressor = stress_sockdiag,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sockdiag_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
#endif
