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
} sockdiag_request_t;

static int sockdiag_send(const args_t *args, const int fd)
{
	static struct sockaddr_nl nladdr = {
		.nl_family = AF_NETLINK
	};

	static sockdiag_request_t request = {
		.nlh = {
			.nlmsg_len = sizeof(sockdiag_request_t),
			.nlmsg_type = SOCK_DIAG_BY_FAMILY,
			.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP
		},
		.udr = {
			.sdiag_family = AF_UNIX,
			.sdiag_protocol = 0,
			.udiag_states = -1,
			.udiag_ino = 0,
			.udiag_cookie = { 0 },
			.udiag_show = UDIAG_SHOW_NAME |
				      UDIAG_SHOW_VFS |
				      UDIAG_SHOW_PEER |
				      UDIAG_SHOW_ICONS |
				      UDIAG_SHOW_RQLEN |
				      UDIAG_SHOW_MEMINFO,
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

	while (keep_stressing()) {
		ssize_t ret;

		ret = sendmsg(fd, &msg, 0);
		if (ret > 0)
			return 1;
		if (errno != EINTR)
			return -1;
	}
	return 0;
}

static int stress_sockdiag_parse(
	const args_t *args,
	struct unix_diag_msg *diag,
	unsigned int len)
{
	struct rtattr *attr;
	unsigned int rta_len;

	if (len < NLMSG_LENGTH(sizeof(*diag))) {
		/* short response, ignore for now */
		return -1;
	}

	if (diag->udiag_family != AF_UNIX) {
		/* bad family, ignore */
		return -1;
	}

	rta_len = len - NLMSG_LENGTH(sizeof(*diag));
	for (attr = (struct rtattr *) (diag + 1);
	     RTA_OK(attr, rta_len) && keep_stressing();
	     attr = RTA_NEXT(attr, rta_len)) {
		switch (attr->rta_type) {
		case UNIX_DIAG_NAME:
			inc_counter(args);
			break;
		case UNIX_DIAG_PEER:
			inc_counter(args);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int sockdiag_recv(const args_t *args, const int fd)
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
			return 0;
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
static int stress_sockdiag(const args_t *args)
{
	int ret = EXIT_SUCCESS;

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
		rc = sockdiag_recv(args, fd);
		if (rc < 0) {
			(void)close(fd);
			break;
		}
		(void)close(fd);
	} while (keep_stressing());

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
