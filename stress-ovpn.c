/*
 * Copyright (C) 2025 Gianmarco De Gregori <gianmarco@mandelbit.com>
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

static const stress_help_t help[] = {
	{ NULL,	"ovpn N",	"start N workers exercising ovpn tasks events" },
	{ NULL,	"ovpn-ops N",	"stop ovpn workers after N bogo events" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_LIB_NL) && defined(HAVE_LINUX_OVPN_H)

#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/ovpn.h>
#include <netlink/socket.h>
#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>

#include <sys/random.h>
#include <sys/select.h>
#include <fcntl.h>

#if defined(HAVE_LINUX_CN_PROC_H)
#include <linux/cn_proc.h>
#endif

#if defined(HAVE_LINUX_CONNECTOR_H)
#include <linux/connector.h>
#endif

#if defined(HAVE_LINUX_NETLINK_H)
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/genetlink.h>
#include <linux/if_link.h>
#include <linux/if_addr.h>
#endif

#if defined(HAVE_SYS_UIO_H)
#include <sys/uio.h>
#endif

#if defined(HAVE_LINUX_VERSION_H)
#include <linux/version.h>
#endif

/* defines to make checkpatch happy */
#define strscpy strncpy
#define __always_unused __attribute__((__unused__))

/* libnl < 3.5.0 does not set the NLA_F_NESTED on its own, therefore we
 * have to explicitly do it to prevent the kernel from failing upon
 * parsing of the message
 */
#define nla_nest_start(_msg, _type) \
	nla_nest_start(_msg, (_type) | NLA_F_NESTED)

/* libnl < 3.11.0 does not implement nla_get_uint() */
static inline uint64_t ovpn_nla_get_uint(struct nlattr *attr)
{
	if (nla_len(attr) == sizeof(uint32_t))
		return nla_get_u32(attr);
	else
		return nla_get_u64(attr);
}

typedef int (*ovpn_nl_cb)(struct nl_msg *msg, void *arg);

enum ovpn_mode {
	OVPN_MODE_P2P,
	OVPN_MODE_MP,
};

enum {
	IFLA_OVPN_UNSPEC,
	IFLA_OVPN_MODE,
	__IFLA_OVPN_MAX,
};

#define IFLA_OVPN_MAX (__IFLA_OVPN_MAX - 1)

enum ovpn_key_direction {
	KEY_DIR_IN = 0,
	KEY_DIR_OUT,
};

#define KEY_LEN (256 / 8)
#define NONCE_LEN 8

#define PEER_ID_UNDEF 0x00FFFFFF
#define MAX_PEERS 10

struct nl_ctx {
	struct nl_sock *nl_sock;
	struct nl_msg *nl_msg;
	struct nl_cb *nl_cb;

	int ovpn_dco_id;
};

enum ovpn_cmd {
	CMD_INVALID,
	CMD_NEW_IFACE,
	CMD_CONNECT,
	CMD_NEW_PEER,
	CMD_SET_PEER,
	CMD_DEL_PEER,
	CMD_GET_PEER,
	CMD_NEW_KEY,
	CMD_DEL_KEY,
	CMD_GET_KEY,
	CMD_SWAP_KEYS,
};

struct ovpn_ctx {
	enum ovpn_cmd cmd;

	__u8 key_enc[KEY_LEN];
	__u8 key_dec[KEY_LEN];
	__u8 nonce[NONCE_LEN];

	enum ovpn_cipher_alg cipher;

	sa_family_t sa_family;

	unsigned long peer_id;
	unsigned long lport;

	union {
		struct sockaddr_in in4;
		struct sockaddr_in6 in6;
	} remote;

	union {
		struct sockaddr_in in4;
		struct sockaddr_in6 in6;
	} peer_ip;

	bool peer_ip_set;

	unsigned int ifindex;
	char ifname[IFNAMSIZ];
	enum ovpn_mode mode;
	bool mode_set;

	int socket;
	int cli_sockets[MAX_PEERS];

	__u32 keepalive_interval;
	__u32 keepalive_timeout;

	enum ovpn_key_direction key_dir;
	enum ovpn_key_slot key_slot;
	int key_id;

	const char *peers_file;
};

struct ovpn_link_req {
	struct nlmsghdr n;
	struct ifinfomsg i;
	char buf[256];
};

/* Helper function used to easily add attributes to a rtnl message */
static int ovpn_addattr(struct nlmsghdr *n, int maxlen, int type,
			const void *data, int alen)
{
	int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if ((int)(NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len)) > maxlen)	{
		pr_dbg("rtnl: message exceeded bound of %d\n", maxlen);
		return -EMSGSIZE;
	}

	rta = nlmsg_tail(n);
	rta->rta_type = type;
	rta->rta_len = len;

	if (!data)
		memset(RTA_DATA(rta), 0, alen);
	else
		memcpy(RTA_DATA(rta), data, alen);

	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

	return 0;
}

static struct rtattr *ovpn_nest_start(struct nlmsghdr *msg, size_t max_size,
				      int attr)
{
	struct rtattr *nest = nlmsg_tail(msg);

	if (ovpn_addattr(msg, max_size, attr, NULL, 0) < 0)
		return NULL;

	return nest;
}

static void ovpn_nest_end(struct nlmsghdr *msg, struct rtattr *nest)
{
	nest->rta_len = (uint8_t *)nlmsg_tail(msg) - (uint8_t *)nest;
}

#define RT_SNDBUF_SIZE (1024 * 2)
#define RT_RCVBUF_SIZE (1024 * 4)

typedef int (*ovpn_parse_reply_cb)(struct nlmsghdr *msg, void *arg);

/* Open RTNL socket */
static int ovpn_rt_socket(void)
{
	int sndbuf = RT_SNDBUF_SIZE, rcvbuf = RT_RCVBUF_SIZE, fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		pr_dbg("cannot open netlink socket\n");
		return fd;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf,
		       sizeof(sndbuf)) < 0) {
		pr_dbg("SO_SNDBUF\n");
		close(fd);
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
		       sizeof(rcvbuf)) < 0) {
		pr_dbg("SO_RCVBUF\n");
		close(fd);
		return -1;
	}

	return fd;
}

/* Bind socket to Netlink subsystem */
static int ovpn_rt_bind(int fd, uint32_t groups)
{
	struct sockaddr_nl local = { 0 };
	socklen_t addr_len;

	local.nl_family = AF_NETLINK;
	local.nl_groups = groups;

	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		pr_dbg("cannot bind netlink socket: %d\n", errno);
		return -errno;
	}

	addr_len = sizeof(local);
	if (getsockname(fd, (struct sockaddr *)&local, &addr_len) < 0) {
		pr_dbg("cannot getsockname: %d\n", errno);
		return -errno;
	}

	if (addr_len != sizeof(local)) {
		pr_dbg("wrong address length %d\n", addr_len);
		return -EINVAL;
	}

	if (local.nl_family != AF_NETLINK) {
		pr_dbg("wrong address family %d\n", local.nl_family);
		return -EINVAL;
	}

	return 0;
}

/* Send Netlink message and run callback on reply (if specified) */
static int ovpn_rt_send(struct nlmsghdr *payload, pid_t peer,
	unsigned int groups, ovpn_parse_reply_cb cb,
	void *arg_cb)
{
	int len, rem_len, fd, ret, rcv_len;
	struct sockaddr_nl nladdr = { 0 };
	struct nlmsgerr *err;
	struct nlmsghdr *h;
	char buf[1024 * 16];
	struct iovec iov = {
		.iov_base = payload,
		.iov_len = payload->nlmsg_len,
	};
	struct msghdr nlmsg = {
		.msg_name = &nladdr,
		.msg_namelen = sizeof(nladdr),
		.msg_iov = &iov,
		.msg_iovlen = 1,
	};

	nladdr.nl_family = AF_NETLINK;
	nladdr.nl_pid = peer;
	nladdr.nl_groups = groups;

	payload->nlmsg_seq = (uint32_t)(time(NULL) & 0xffffffff);

	/* no need to send reply */
	if (!cb)
		payload->nlmsg_flags |= NLM_F_ACK;

	fd = ovpn_rt_socket();
	if (fd < 0) {
		pr_dbg("can't open rtnl socket\n");
		return -errno;
	}

	ret = ovpn_rt_bind(fd, 0);
	if (ret < 0) {
		pr_dbg("can't bind rtnl socket\n");
		ret = -errno;
		goto out;
	}

	ret = sendmsg(fd, &nlmsg, 0);
	if (ret < 0) {
		pr_dbg("rtnl: error on sendmsg()\n");
		ret = -errno;
		goto out;
	}

	/* prepare buffer to store RTNL replies */
	memset(buf, 0, sizeof(buf));
	iov.iov_base = buf;

	while (1) {
		/*
		 * iov_len is modified by recvmsg(), therefore has to be initialized before
		 * using it again
		 */
		iov.iov_len = sizeof(buf);
		rcv_len = recvmsg(fd, &nlmsg, 0);
		if (rcv_len < 0) {
			if (errno == EINTR || errno == EAGAIN) {
				pr_dbg("interrupted call\n");
				continue;
			}
			pr_dbg("rtnl: error on recvmsg()\n");
			ret = -errno;
			goto out;
		}

		if (rcv_len == 0) {
			pr_dbg("rtnl: socket reached unexpected EOF\n");
			ret = -EIO;
			goto out;
		}

		if (nlmsg.msg_namelen != sizeof(nladdr)) {
			pr_dbg("sender address length: %u (expected %zu)\n",
				nlmsg.msg_namelen, sizeof(nladdr));
			ret = -EIO;
			goto out;
		}

		h = (struct nlmsghdr *)(uintptr_t)buf;
		while (rcv_len >= (int)sizeof(*h)) {
			len = h->nlmsg_len;
			rem_len = len - sizeof(*h);

			if (rem_len < 0 || len > rcv_len) {
				if (nlmsg.msg_flags & MSG_TRUNC) {
					pr_dbg("truncated message\n");
					ret = -EIO;
					goto out;
				}
				pr_dbg("malformed message: len=%d\n", len);
				ret = -EIO;
				goto out;
			}

			if (h->nlmsg_type == NLMSG_DONE) {
				ret = 0;
				goto out;
			}

			if (h->nlmsg_type == NLMSG_ERROR) {
				err = (struct nlmsgerr *)NLMSG_DATA(h);
				if (rem_len < (int)sizeof(struct nlmsgerr)) {
					pr_dbg("ERROR truncated\n");
					ret = -EIO;
					goto out;
				}

				if (err->error) {
					pr_dbg("(%d) %s\n",
						err->error,
						strerror(-err->error));
					ret = err->error;
					goto out;
				}

				ret = 0;
				if (cb) {
					int r = cb(h, arg_cb);

					if (r <= 0)
						ret = r;
				}
				goto out;
			}

			if (cb) {
				int r = cb(h, arg_cb);

				if (r <= 0) {
					ret = r;
					goto out;
				}
			} else {
				pr_dbg("RTNL: unexpected reply\n");
			}

			rcv_len -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr *)(uintptr_t)((uint8_t *)h + NLMSG_ALIGN(len));
		}

		if (nlmsg.msg_flags & MSG_TRUNC) {
			pr_dbg("message truncated\n");
			continue;
		}

		if (rcv_len) {
			pr_dbg("rtnl: %d not parsed bytes\n", rcv_len);
			ret = -1;
			goto out;
		}
	}
out:
	close(fd);

	return ret;
}

static int ovpn_socket(struct ovpn_ctx *ctx, sa_family_t family, int proto)
{
	struct sockaddr_storage local_sock = { 0 };
	struct sockaddr_in6 *in6;
	struct sockaddr_in *in;
	int ret, s, sock_type;
	size_t sock_len;

	if (proto == IPPROTO_UDP)
		sock_type = SOCK_DGRAM;
	else if (proto == IPPROTO_TCP)
		sock_type = SOCK_STREAM;
	else
		return -EINVAL;

	s = socket(family, sock_type, 0);
	if (s < 0) {
		pr_err("cannot create socket: %s\n", strerror(errno));
		return -1;
	}

	switch (family) {
	case AF_INET:
		in = (struct sockaddr_in *)&local_sock;
		in->sin_family = family;
		in->sin_port = htons(ctx->lport);
		in->sin_addr.s_addr = htonl(INADDR_ANY);
		sock_len = sizeof(*in);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&local_sock;
		in6->sin6_family = family;
		in6->sin6_port = htons(ctx->lport);
		in6->sin6_addr = in6addr_any;
		sock_len = sizeof(*in6);
		break;
	default:
		close(s);
		return -1;
	}

	int opt = 1;

	ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0) {
		pr_err("setsockopt for SO_REUSEADDR: %s\n", strerror(errno));
		goto err_socket;
	}

	ret = setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		pr_err("setsockopt for SO_REUSEPORT: %s\n", strerror(errno));
		goto err_socket;
	}

	if (family == AF_INET6) {
		opt = 0;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &opt,
			       sizeof(opt))) {
			pr_err("failed to set IPV6_V6ONLY: %s\n", strerror(errno));
			goto err_socket;
		}
	}

	ret = bind(s, (struct sockaddr *)&local_sock, sock_len);
	if (ret < 0) {
		pr_err("cannot bind socket: %s\n", strerror(errno));
		goto err_socket;
	}

	ctx->socket = s;
	ctx->sa_family = family;
	return 0;

err_socket:
	close(s);
	return -1;
}

static int ovpn_new_iface(struct ovpn_ctx *ovpn)
{
	struct rtattr *linkinfo, *data;
	struct ovpn_link_req req = { 0 };
	int ret = -1;

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	req.n.nlmsg_type = RTM_NEWLINK;

	if (ovpn_addattr(&req.n, sizeof(req), IFLA_IFNAME, ovpn->ifname,
			 strlen(ovpn->ifname) + 1) < 0)
		goto err;

	linkinfo = ovpn_nest_start(&req.n, sizeof(req), IFLA_LINKINFO);
	if (!linkinfo)
		goto err;

	if (ovpn_addattr(&req.n, sizeof(req), IFLA_INFO_KIND, OVPN_FAMILY_NAME,
			 strlen(OVPN_FAMILY_NAME) + 1) < 0)
		goto err;

	if (ovpn->mode_set) {
		data = ovpn_nest_start(&req.n, sizeof(req), IFLA_INFO_DATA);
		if (!data)
			goto err;

		if (ovpn_addattr(&req.n, sizeof(req), IFLA_OVPN_MODE,
				 &ovpn->mode, sizeof(uint8_t)) < 0)
			goto err;

		ovpn_nest_end(&req.n, data);
	}

	ovpn_nest_end(&req.n, linkinfo);

	req.i.ifi_family = AF_PACKET;

	ret = ovpn_rt_send(&req.n, 0, 0, NULL, NULL);
err:
	return ret;
}

static struct nl_ctx *nl_ctx_alloc_flags(struct ovpn_ctx *ovpn, int cmd,
	int flags)
{
	struct nl_ctx *ctx;
	int err, ret;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->nl_sock = nl_socket_alloc();
	if (!ctx->nl_sock) {
		pr_err("cannot allocate netlink socket\n");
		goto err_free;
	}

	nl_socket_set_buffer_size(ctx->nl_sock, 8192, 8192);

	ret = genl_connect(ctx->nl_sock);
	if (ret) {
		pr_dbg("cannot connect to generic netlink: %s\n",
			nl_geterror(ret));
		goto err_sock;
	}

	/* enable Extended ACK for detailed error reporting; ignore failure */
	err = 1;
	(void)setsockopt(nl_socket_get_fd(ctx->nl_sock), SOL_NETLINK, NETLINK_EXT_ACK,
		   &err, sizeof(err));

	ctx->ovpn_dco_id = genl_ctrl_resolve(ctx->nl_sock, OVPN_FAMILY_NAME);
	if (ctx->ovpn_dco_id < 0) {
		pr_dbg("cannot find ovpn_dco netlink component: %d\n",
			ctx->ovpn_dco_id);
		goto err_sock;
	}

	ctx->nl_msg = nlmsg_alloc();
	if (!ctx->nl_msg) {
		pr_err("cannot allocate netlink message\n");
		goto err_sock;
	}

	ctx->nl_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!ctx->nl_cb) {
		pr_err("failed to allocate netlink callback\n");
		goto err_msg;
	}

	nl_socket_set_cb(ctx->nl_sock, ctx->nl_cb);

	genlmsg_put(ctx->nl_msg, 0, 0, ctx->ovpn_dco_id, 0, flags, cmd, 0);

	if (ovpn->ifindex > 0)
		NLA_PUT_U32(ctx->nl_msg, OVPN_A_IFINDEX, ovpn->ifindex);

	return ctx;
nla_put_failure:
err_msg:
	nlmsg_free(ctx->nl_msg);
err_sock:
	nl_socket_free(ctx->nl_sock);
err_free:
	free(ctx);
	return NULL;
}

static struct nl_ctx *nl_ctx_alloc(struct ovpn_ctx *ovpn, int cmd)
{
	return nl_ctx_alloc_flags(ovpn, cmd, 0);
}

static int ovpn_nl_cb_finish(struct nl_msg (*msg)__always_unused, void *arg)
{
	int *status = arg;

	*status = 0;
	return NL_SKIP;
}

static int ovpn_nl_cb_ack(struct nl_msg (*msg)__always_unused,
			  void *arg)
{
	int *status = arg;

	*status = 0;
	return NL_STOP;
}

static int ovpn_nl_recvmsgs(struct nl_ctx *ctx)
{
	int ret;

	ret = nl_recvmsgs(ctx->nl_sock, ctx->nl_cb);

	switch (ret) {
	case -NLE_INTR:
		pr_dbg("netlink received interrupt due to signal - ignoring\n");
		break;
	case -NLE_NOMEM:
		pr_dbg("netlink out of memory error\n");
		break;
	case -NLE_AGAIN:
		pr_dbg("netlink reports blocking read - aborting wait\n");
		break;
	default:
		if (ret)
			pr_dbg("netlink reports error (%d): %s\n",
				ret, nl_geterror(-ret));
		break;
	}

	return ret;
}

static int ovpn_nl_cb_error(struct sockaddr_nl (*nla)__always_unused,
			    struct nlmsgerr *err, void *arg)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)err - 1;
	struct nlattr *tb_msg[NLMSGERR_ATTR_MAX + 1];
	int len = nlh->nlmsg_len;
	struct nlattr *attrs;
	int *ret = arg;
	int ack_len = sizeof(*nlh) + sizeof(int) + sizeof(*nlh);

	*ret = err->error;

	if (!(nlh->nlmsg_flags & NLM_F_ACK_TLVS))
		return NL_STOP;

	if (!(nlh->nlmsg_flags & NLM_F_CAPPED))
		ack_len += err->msg.nlmsg_len - sizeof(*nlh);

	if (len <= ack_len)
		return NL_STOP;

	attrs = (void *)((uint8_t *)nlh + ack_len);
	len -= ack_len;

	nla_parse(tb_msg, NLMSGERR_ATTR_MAX, attrs, len, NULL);
	if (tb_msg[NLMSGERR_ATTR_MSG]) {
		len = strnlen((char *)nla_data(tb_msg[NLMSGERR_ATTR_MSG]),
			      nla_len(tb_msg[NLMSGERR_ATTR_MSG]));
		pr_dbg("kernel error: %*s\n", len,
			(char *)nla_data(tb_msg[NLMSGERR_ATTR_MSG]));
	}

#ifdef NLMSGERR_ATTR_MISS_NEST
	if (tb_msg[NLMSGERR_ATTR_MISS_NEST]) {
		pr_dbg("missing required nesting type %u\n",
			nla_get_u32(tb_msg[NLMSGERR_ATTR_MISS_NEST]));
	}
#endif

#ifdef NLMSGERR_ATTR_MISS_TYPE
	if (tb_msg[NLMSGERR_ATTR_MISS_TYPE]) {
		pr_dbg("missing required attribute type %u\n",
			nla_get_u32(tb_msg[NLMSGERR_ATTR_MISS_TYPE]));
	}
#endif

	return NL_STOP;
}

static int ovpn_nl_msg_send(struct nl_ctx *ctx, ovpn_nl_cb cb)
{
	int status = 1;

	nl_cb_err(ctx->nl_cb, NL_CB_CUSTOM, ovpn_nl_cb_error, &status);
	nl_cb_set(ctx->nl_cb, NL_CB_FINISH, NL_CB_CUSTOM, ovpn_nl_cb_finish, &status);
	nl_cb_set(ctx->nl_cb, NL_CB_ACK, NL_CB_CUSTOM, ovpn_nl_cb_ack, &status);

	if (cb)
		nl_cb_set(ctx->nl_cb, NL_CB_VALID, NL_CB_CUSTOM, cb, ctx);

	nl_send_auto_complete(ctx->nl_sock, ctx->nl_msg);

	while ((status == 1) && stress_continue_flag())
		ovpn_nl_recvmsgs(ctx);

	if (status < 0)
		pr_dbg("failed to send netlink message: %s (%d)\n",
			strerror(-status), status);

	return status;
}

static void nl_ctx_free(struct nl_ctx *ctx)
{
	if (!ctx)
		return;

	nl_socket_free(ctx->nl_sock);
	nlmsg_free(ctx->nl_msg);
	nl_cb_put(ctx->nl_cb);
	free(ctx);
}

static int ovpn_new_peer(struct ovpn_ctx *ovpn, bool is_tcp)
{
	struct nlattr *attr;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_PEER_NEW);
	if (!ctx)
		return -ENOMEM;

	attr = nla_nest_start(ctx->nl_msg, OVPN_A_PEER);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_SOCKET, ovpn->socket);

	if (!is_tcp) {
		switch (ovpn->remote.in4.sin_family) {
		case AF_INET:
			NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_REMOTE_IPV4,
				    ovpn->remote.in4.sin_addr.s_addr);
			NLA_PUT_U16(ctx->nl_msg, OVPN_A_PEER_REMOTE_PORT,
				    ovpn->remote.in4.sin_port);
			break;
		case AF_INET6:
			NLA_PUT(ctx->nl_msg, OVPN_A_PEER_REMOTE_IPV6,
				sizeof(ovpn->remote.in6.sin6_addr),
				&ovpn->remote.in6.sin6_addr);
			NLA_PUT_U32(ctx->nl_msg,
				    OVPN_A_PEER_REMOTE_IPV6_SCOPE_ID,
				    ovpn->remote.in6.sin6_scope_id);
			NLA_PUT_U16(ctx->nl_msg, OVPN_A_PEER_REMOTE_PORT,
				    ovpn->remote.in6.sin6_port);
			break;
		default:
			pr_dbg("Invalid family for remote socket address\n");
			goto nla_put_failure;
		}
	}

	if (ovpn->peer_ip_set) {
		switch (ovpn->peer_ip.in4.sin_family) {
		case AF_INET:
			NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_VPN_IPV4,
				    ovpn->peer_ip.in4.sin_addr.s_addr);
			break;
		case AF_INET6:
			NLA_PUT(ctx->nl_msg, OVPN_A_PEER_VPN_IPV6,
				sizeof(struct in6_addr),
				&ovpn->peer_ip.in6.sin6_addr);
			break;
		default:
			pr_dbg("Invalid family for peer address\n");
			goto nla_put_failure;
		}
	}

	nla_nest_end(ctx->nl_msg, attr);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_parse_remote(struct ovpn_ctx *ovpn, const char *host,
	const char *service, const char *vpnip)
{
	int ret;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = ovpn->sa_family,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP
	};

	if (host) {
		ret = getaddrinfo(host, service, &hints, &result);
		if (ret) {
			pr_dbg("getaddrinfo on remote error: %s\n",
				gai_strerror(ret));
			return -1;
		}

		if (!(result->ai_family == AF_INET &&
		      result->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    !(result->ai_family == AF_INET6 &&
		      result->ai_addrlen == sizeof(struct sockaddr_in6))) {
			freeaddrinfo(result);
			return -EINVAL;
		}

		memcpy(&ovpn->remote, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
	}

	if (vpnip) {
		ret = getaddrinfo(vpnip, NULL, &hints, &result);
		if (ret) {
			pr_dbg("getaddrinfo on vpnip error: %s\n",
				gai_strerror(ret));
			return -1;
		}

		if (!(result->ai_family == AF_INET &&
		      result->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    !(result->ai_family == AF_INET6 &&
		      result->ai_addrlen == sizeof(struct sockaddr_in6))) {
			freeaddrinfo(result);
			return -EINVAL;
		}

		memcpy(&ovpn->peer_ip, result->ai_addr, result->ai_addrlen);
		ovpn->sa_family = result->ai_family;
		ovpn->peer_ip_set = true;
		freeaddrinfo(result);
	}

	return 0;
}

static int ovpn_parse_new_peer(struct ovpn_ctx *ovpn, int peer_id,
	const char *raddr, const char *rport,
	const char *vpnip)
{
	ovpn->peer_id = peer_id;
	if (ovpn->peer_id > PEER_ID_UNDEF) {
		pr_dbg("peer ID value out of range\n");
		return -1;
	}

	return ovpn_parse_remote(ovpn, raddr, rport, vpnip);
}


static int ovpn_connect(struct ovpn_ctx *ovpn)
{
	socklen_t socklen;
	int s, ret, flags;
	fd_set wfds;
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };

	s = socket(ovpn->remote.in4.sin_family, SOCK_STREAM, 0);
	if (s < 0) {
		pr_err("cannot create socket: %s\n", strerror(errno));
		return -1;
	}

	switch (ovpn->remote.in4.sin_family) {
	case AF_INET:
		socklen = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		socklen = sizeof(struct sockaddr_in6);
		break;
	default:
		ret = -EOPNOTSUPP;
		goto err;
	}

	flags = fcntl(s, F_GETFL, 0);
	if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
		pr_err("fcntl: %s\n", strerror(errno));
		ret = -1;
		goto err;
	}

	ret = connect(s, (struct sockaddr *)&ovpn->remote, socklen);
	if (ret < 0 && errno != EINPROGRESS) {
		pr_dbg("connect: %s\n", strerror(errno));
		goto err;
	}

	FD_ZERO(&wfds);
	FD_SET(s, &wfds);
	ret = select(s + 1, NULL, &wfds, NULL, &tv);
	if (ret <= 0) {
		ret = (ret == 0) ? -ETIMEDOUT : -errno;
		goto err;
	}

	/* restore blocking mode */
	(void)fcntl(s, F_SETFL, flags);

	ovpn->socket = s;

	return 0;
err:
	close(s);
	return ret;
}

static int ovpn_new_key(struct ovpn_ctx *ovpn)
{
	struct nlattr *keyconf, *key_dir;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_NEW);
	if (!ctx)
		return -ENOMEM;

	keyconf = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_SLOT, ovpn->key_slot);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_KEY_ID, ovpn->key_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_CIPHER_ALG, ovpn->cipher);

	key_dir = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF_ENCRYPT_DIR);
	NLA_PUT(ctx->nl_msg, OVPN_A_KEYDIR_CIPHER_KEY, KEY_LEN, ovpn->key_enc);
	NLA_PUT(ctx->nl_msg, OVPN_A_KEYDIR_NONCE_TAIL, NONCE_LEN, ovpn->nonce);
	nla_nest_end(ctx->nl_msg, key_dir);

	key_dir = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF_DECRYPT_DIR);
	NLA_PUT(ctx->nl_msg, OVPN_A_KEYDIR_CIPHER_KEY, KEY_LEN, ovpn->key_dec);
	NLA_PUT(ctx->nl_msg, OVPN_A_KEYDIR_NONCE_TAIL, NONCE_LEN, ovpn->nonce);
	nla_nest_end(ctx->nl_msg, key_dir);

	nla_nest_end(ctx->nl_msg, keyconf);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_send_tcp_data(int socket)
{
	uint16_t len = htons(1000);
	uint8_t buf[1002];
	int ret;

	memcpy(buf, &len, sizeof(len));
	memset(buf + sizeof(len), 0x86, sizeof(buf) - sizeof(len));

	ret = send(socket, buf, sizeof(buf), MSG_NOSIGNAL);

	return ret > 0 ? 0 : ret;
}

static int ovpn_udp_socket(struct ovpn_ctx *ctx, sa_family_t family)
{
	return ovpn_socket(ctx, family, IPPROTO_UDP);
}

static int ovpn_set_peer(struct ovpn_ctx *ovpn)
{
	struct nlattr *attr;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_PEER_SET);
	if (!ctx)
		return -ENOMEM;

	attr = nla_nest_start(ctx->nl_msg, OVPN_A_PEER);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_KEEPALIVE_INTERVAL,
		    ovpn->keepalive_interval);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_KEEPALIVE_TIMEOUT,
		    ovpn->keepalive_timeout);
	nla_nest_end(ctx->nl_msg, attr);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_del_peer(struct ovpn_ctx *ovpn)
{
	struct nlattr *attr;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_PEER_DEL);
	if (!ctx)
		return -ENOMEM;

	attr = nla_nest_start(ctx->nl_msg, OVPN_A_PEER);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_ID, ovpn->peer_id);
	nla_nest_end(ctx->nl_msg, attr);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_swap_keys(struct ovpn_ctx *ovpn)
{
	struct nl_ctx *ctx;
	struct nlattr *kc;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_SWAP);
	if (!ctx)
		return -ENOMEM;

	kc = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	nla_nest_end(ctx->nl_msg, kc);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_del_key(struct ovpn_ctx *ovpn)
{
	struct nlattr *keyconf;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_DEL);
	if (!ctx)
		return -ENOMEM;

	keyconf = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_SLOT, ovpn->key_slot);
	nla_nest_end(ctx->nl_msg, keyconf);

	ret = ovpn_nl_msg_send(ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_handle_peer(struct nl_msg *msg, void (*arg)__always_unused)
{
	struct nlattr *pattrs[OVPN_A_PEER_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *attrs[OVPN_A_MAX + 1];

	nla_parse(attrs, OVPN_A_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!attrs[OVPN_A_PEER])
		return NL_SKIP;

	nla_parse(pattrs, OVPN_A_PEER_MAX, nla_data(attrs[OVPN_A_PEER]),
		  nla_len(attrs[OVPN_A_PEER]), NULL);

	return NL_SKIP;
}

static int ovpn_get_peer(struct ovpn_ctx *ovpn)
{
	int flags = 0, ret = -1;
	struct nlattr *attr;
	struct nl_ctx *ctx;

	if (ovpn->peer_id == PEER_ID_UNDEF)
		flags = NLM_F_DUMP;

	ctx = nl_ctx_alloc_flags(ovpn, OVPN_CMD_PEER_GET, flags);
	if (!ctx)
		return -ENOMEM;

	if (ovpn->peer_id != PEER_ID_UNDEF) {
		attr = nla_nest_start(ctx->nl_msg, OVPN_A_PEER);
		NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_ID, ovpn->peer_id);
		nla_nest_end(ctx->nl_msg, attr);
	}

	ret = ovpn_nl_msg_send(ctx, ovpn_handle_peer);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_handle_key(struct nl_msg *msg, void (*arg)__always_unused)
{
	struct nlattr *kattrs[OVPN_A_KEYCONF_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *attrs[OVPN_A_MAX + 1];

	nla_parse(attrs, OVPN_A_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!attrs[OVPN_A_KEYCONF])
		return NL_SKIP;

	nla_parse(kattrs, OVPN_A_KEYCONF_MAX, nla_data(attrs[OVPN_A_KEYCONF]),
		  nla_len(attrs[OVPN_A_KEYCONF]), NULL);

	return NL_SKIP;
}

static int ovpn_get_key(struct ovpn_ctx *ovpn)
{
	struct nlattr *keyconf;
	struct nl_ctx *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_GET);
	if (!ctx)
		return -ENOMEM;

	keyconf = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_SLOT, ovpn->key_slot);
	nla_nest_end(ctx->nl_msg, keyconf);

	ret = ovpn_nl_msg_send(ctx, ovpn_handle_key);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_run_cmd(struct ovpn_ctx *ovpn)
{
	int ret = 0;

	switch (ovpn->cmd) {
	case CMD_NEW_IFACE:
		ret = ovpn_new_iface(ovpn);
		break;
	case CMD_CONNECT:
		ret = ovpn_connect(ovpn);
		if (ret < 0) {
			pr_dbg("cannot connect TCP socket\n");
			return ret;
		}

		ret = ovpn_new_peer(ovpn, true);
		if (ret < 0) {
			pr_dbg("cannot add peer to VPN\n");
			close(ovpn->socket);
			return ret;
		}

		if (ovpn->cipher != OVPN_CIPHER_ALG_NONE) {
			ret = ovpn_new_key(ovpn);
			if (ret < 0) {
				pr_dbg("cannot set key\n");
				return ret;
			}
		}

		ret = ovpn_send_tcp_data(ovpn->socket);
		break;
	case CMD_NEW_PEER:
		ret = ovpn_udp_socket(ovpn, AF_INET6);
		if (ret < 0)
			return ret;

		ret = ovpn_new_peer(ovpn, false);
		break;
	case CMD_SET_PEER:
		ret = ovpn_set_peer(ovpn);
		break;
	case CMD_DEL_PEER:
		ret = ovpn_del_peer(ovpn);
		break;
	case CMD_GET_PEER:
		ret = ovpn_get_peer(ovpn);
		break;
	case CMD_NEW_KEY:
		ret = ovpn_new_key(ovpn);
		break;
	case CMD_DEL_KEY:
		ret = ovpn_del_key(ovpn);
		break;
	case CMD_GET_KEY:
		ret = ovpn_get_key(ovpn);
		break;
	case CMD_SWAP_KEYS:
		ret = ovpn_swap_keys(ovpn);
		break;
	case CMD_INVALID:
		break;
	}

	return ret;
}


static int stress_ovpn_supported(const char *name)
{
	if (!stress_capabilities_check(SHIM_CAP_NET_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, need to be running with CAP_NET_ADMIN rights for this stressor\n",
			name);
		return -1;
	}
	return 0;
}


static void ovpn_ctx_reset(struct ovpn_ctx *ovpn)
{
	memset(ovpn, 0, sizeof(*ovpn));

	ovpn->socket = -1;

	strscpy(ovpn->ifname, "tun0", IFNAMSIZ);
	ovpn->ifindex = if_nametoindex(ovpn->ifname);

	ovpn->sa_family = AF_INET;
	ovpn->cipher = OVPN_CIPHER_ALG_NONE;
}

static int build_new_iface(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_NEW_IFACE;
	ovpn->mode = (stress_mwc32() & 1) ? OVPN_MODE_P2P : OVPN_MODE_MP;
	ovpn->mode_set = true;

	return 0;
}


static int ovpn_generate_key(struct ovpn_ctx *ctx)
{
	if (getrandom(ctx->key_enc, KEY_LEN, 0) != KEY_LEN) {
		pr_err("getrandom(key_enc): %s\n", strerror(errno));
		return -1;
	}

	if (getrandom(ctx->key_dec, KEY_LEN, 0) != KEY_LEN) {
		pr_err("getrandom(key_dec): %s\n", strerror(errno));
		return -1;
	}

	if (getrandom(ctx->nonce, NONCE_LEN, 0) != NONCE_LEN) {
		pr_err("getrandom(nonce): %s\n", strerror(errno));
		return -1;
	}

	return 0;
}

static void ovpn_rand_addr_port(char *addr, size_t alen, char *port, size_t plen)
{
	snprintf(addr, alen, "10.%u.%u.%u",
		stress_mwc8() + 1, stress_mwc8(), stress_mwc8() + 1);
	snprintf(port, plen, "%u",
		(stress_mwc16() % 64511) + 1024);
}

static int build_connect(struct ovpn_ctx *ovpn)
{
	char addr[INET_ADDRSTRLEN], port[6];

	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_CONNECT;

	ovpn_rand_addr_port(addr, sizeof(addr), port, sizeof(port));
	if (ovpn_parse_new_peer(ovpn, stress_mwc32() % 10, addr, port, NULL))
		return -1;

	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;
	ovpn->key_id   = 0;
	ovpn->cipher   = OVPN_CIPHER_ALG_AES_GCM;
	ovpn->key_dir  = KEY_DIR_OUT;

	return ovpn_generate_key(ovpn);
}

static int build_new_peer(struct ovpn_ctx *ovpn)
{
	char addr[INET_ADDRSTRLEN], port[6];

	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_NEW_PEER;
	ovpn->lport = 1194;

	ovpn_rand_addr_port(addr, sizeof(addr), port, sizeof(port));
	return ovpn_parse_new_peer(ovpn, stress_mwc32() % 10, addr, port, NULL);
}

static int build_set_peer(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_SET_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->keepalive_interval = 10;
	ovpn->keepalive_timeout  = 60;

	return 0;
}

static int build_del_peer(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_DEL_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int build_get_peer(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_GET_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int build_new_key(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_NEW_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;
	ovpn->key_id = 0;
	ovpn->cipher = OVPN_CIPHER_ALG_AES_GCM;
	ovpn->key_dir = KEY_DIR_OUT;

	return ovpn_generate_key(ovpn);
}

static int build_del_key(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_DEL_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;

	return 0;
}

static int build_get_key(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_GET_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;

	return 0;
}

static int build_swap_keys(struct ovpn_ctx *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_SWAP_KEYS;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int ovpn_autofill_args(struct ovpn_ctx *ovpn)
{
	switch (ovpn->cmd) {
	case CMD_NEW_IFACE:   return build_new_iface(ovpn);
	case CMD_CONNECT:     return build_connect(ovpn);
	case CMD_NEW_PEER:    return build_new_peer(ovpn);
	case CMD_SET_PEER:    return build_set_peer(ovpn);
	case CMD_DEL_PEER:    return build_del_peer(ovpn);
	case CMD_GET_PEER:    return build_get_peer(ovpn);
	case CMD_NEW_KEY:     return build_new_key(ovpn);
	case CMD_DEL_KEY:     return build_del_key(ovpn);
	case CMD_GET_KEY:     return build_get_key(ovpn);
	case CMD_SWAP_KEYS:   return build_swap_keys(ovpn);
	default:
		return 0;
	}
}

static int stress_ovpn(stress_args_t *args)
{
	struct ovpn_ctx ovpn;
	int last_cmd = -1;
	static const enum ovpn_cmd cmds[] = {
		CMD_INVALID, CMD_NEW_IFACE,
		CMD_CONNECT, CMD_NEW_PEER,
		CMD_SET_PEER, CMD_DEL_PEER,
		CMD_GET_PEER, CMD_NEW_KEY,
		CMD_DEL_KEY, CMD_GET_KEY,
		CMD_SWAP_KEYS,
	};
	const size_t count = SIZEOF_ARRAY(cmds);

	(void)memset(&ovpn, 0, sizeof(ovpn));
	ovpn.sa_family = AF_INET;
	ovpn.cipher = OVPN_CIPHER_ALG_NONE;
	ovpn.peers_file = NULL;
	ovpn.socket = -1;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		int cmd;

		do {
			uint32_t idx = stress_mwc32() % count;

			cmd = cmds[idx];
		} while (cmd == last_cmd && count > 1);

		last_cmd = cmd;
		ovpn.cmd = cmd;

		ovpn_autofill_args(&ovpn);

		if (ovpn_run_cmd(&ovpn) != 0)
			shim_sched_yield();

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (ovpn.socket >= 0)
		(void)close(ovpn.socket);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_ovpn_info = {
	.stressor = stress_ovpn,
	.supported = stress_ovpn_supported,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help
};


#else

const stressor_info_t stress_ovpn_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.help = help,
	.unimplemented_reason = "built without libnl3 or linux/ovpn.h support"
};

#endif /* HAVE_LIB_NL && HAVE_LINUX_OVPN_H */
