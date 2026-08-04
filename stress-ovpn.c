/*
 * Copyright (C) 2025-2026 Gianmarco De Gregori <gianmarco@mandelbit.com>
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
#include "core-affinity.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-helper.h"
#include "core-killpid.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL,	"ovpn N",	"start N workers exercising ovpn tasks events" },
	{ NULL,	"ovpn-ops N",	"stop ovpn workers after N bogo events" },
	{ NULL,	"ovpn-tunnel",	"build a real two-endpoint DCO tunnel (two netns) and exercise the data path" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_ovpn_tunnel, "ovpn-tunnel", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

/*
 * static builds with libnl require getprotobynumber_r
 * which is not supported as a static lib at present.
 *
 * HAVE_LINUX_OVPN_UAPI rather than HAVE_LINUX_OVPN_H: the header travelled
 * through several out-of-tree revisions before ovpn was merged in 6.16, so
 * its mere presence does not imply it declares the commands and attributes
 * used below. The configure test references them, so an older or partial
 * copy makes the stressor report itself unimplemented instead of failing
 * the build.
 */
#if defined(HAVE_LIB_NL) &&		\
    defined(HAVE_LINUX_OVPN_UAPI) &&	\
    !defined(BUILD_STATIC)

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
#include <sched.h>

#if defined(HAVE_LINUX_VETH_H)
#include <linux/veth.h>
#endif
#if !defined(VETH_INFO_PEER)
#define VETH_INFO_PEER	(1)
#endif

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

/* libnl < 3.5.0 does not set the NLA_F_NESTED on its own, therefore we
 * have to explicitly do it to prevent the kernel from failing upon
 * parsing of the message
 */
#define nla_nest_start(_msg, _type) \
	nla_nest_start(_msg, (_type) | NLA_F_NESTED)

#define RT_SNDBUF_SIZE (1024 * 2)
#define RT_RCVBUF_SIZE (1024 * 4)
/*
 * A reply is not guaranteed to arrive: the kernel can fail to allocate one
 * under memory pressure, and the request may have been aimed at an object that
 * went away. Without a deadline the recvmsg() loop in ovpn_rt_send() waits for
 * it indefinitely, which hangs the worker.
 */
#define RT_RCVTIMEO_SEC (5)

#define KEY_LEN		(256 / 8)
#define NONCE_LEN	(8)

#define PEER_ID_UNDEF	(0x00FFFFFF)
#define MAX_PEERS	(10)

/* libnl < 3.11.0 does not implement nla_get_uint() */

extern uint64_t ovpn_nla_get_uint(struct nlattr *attr) WEAK;

uint64_t ovpn_nla_get_uint(struct nlattr *attr)
{
	if (nla_len(attr) == sizeof(uint32_t))
		return nla_get_u32(attr);
	else
		return nla_get_u64(attr);
}

struct ovpn_ctx;

typedef int (*ovpn_nl_cb)(struct nl_msg *msg, void *arg);
typedef int (*ovpn_parse_reply_cb)(struct nlmsghdr *msg, void *arg);

typedef enum shim_ovpn_mode {
	SHIM_OVPN_MODE_P2P,
	SHIM_OVPN_MODE_MP,
} shim_ovpn_mode_t;

typedef enum ovpn_key_direction {
	SHIM_KEY_DIR_IN = 0,
	SHIM_KEY_DIR_OUT,
} ovpn_key_direction_t;

typedef struct nl_ctx {
	struct nl_sock *nl_sock;
	struct nl_msg *nl_msg;
	struct nl_cb *nl_cb;

	struct ovpn_ctx *ovpn;		/* owner, for reply callbacks */
	int nl_status;			/* set by the reply callbacks */

	int ovpn_dco_id;
} nl_ctx_t;

typedef enum ovpn_cmd {
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
} ovpn_cmd_t;

typedef struct ovpn_ctx {
	ovpn_cmd_t cmd;

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
	shim_ovpn_mode_t mode;
	bool mode_set;

	int socket;
	int cli_sockets[MAX_PEERS];

	__u32 keepalive_interval;
	__u32 keepalive_timeout;

	ovpn_key_direction_t key_dir;
	enum ovpn_key_slot key_slot;
	int key_id;

	const char *peers_file;

	/*
	 * per-peer byte counters read back by OVPN_CMD_PEER_GET. These are the
	 * kernel's own accounting: VPN_* is the plaintext side (what the crypto
	 * path processed), so they are authoritative where a userspace estimate
	 * only counts what was handed to or read from a socket.
	 */
	uint64_t peer_vpn_rx_bytes;
	uint64_t peer_vpn_tx_bytes;
	bool peer_stats_valid;

	/*
	 * Set while operating on the phantom peers, whose ids are random and
	 * mostly do not exist: those commands are meant to fail and their
	 * failures are not worth narrating. Without this a single minute of
	 * churn buries the log in tens of thousands of expected ENOENTs and
	 * makes --verbose useless for seeing anything else.
	 */
	bool expect_failure;

	const char *args_name;
} ovpn_ctx_t;

typedef struct ovpn_link_req {
	struct nlmsghdr n;
	struct ifinfomsg i;
	char buf[256];
} ovpn_link_req_t;

/* Helper function used to easily add attributes to a rtnl message */
static int ovpn_addattr(
	ovpn_ctx_t *ovpn,
	struct nlmsghdr *n,
	const int maxlen,
	const int type,
	const void *data,
	const int alen)
{
	const int len = RTA_LENGTH(alen);
	struct rtattr *rta;

	if ((int)(NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len)) > maxlen)	{
		pr_dbg("%s: message exceeded bound of %d\n", ovpn->args_name, maxlen);
		return -EMSGSIZE;
	}

	rta = nlmsg_tail(n);
	rta->rta_type = type;
	rta->rta_len = len;

	if (!data)
		(void)shim_memset(RTA_DATA(rta), 0, alen);
	else
		(void)shim_memcpy(RTA_DATA(rta), data, alen);

	n->nlmsg_len = NLMSG_ALIGN(n->nlmsg_len) + RTA_ALIGN(len);

	return 0;
}

static struct rtattr *ovpn_nest_start(
	ovpn_ctx_t *ovpn,
	struct nlmsghdr *msg,
	const size_t max_size,
	const int attr)
{
	struct rtattr *nest = nlmsg_tail(msg);

	if (ovpn_addattr(ovpn, msg, max_size, attr, NULL, 0) < 0)
		return NULL;

	return nest;
}

static inline void ovpn_nest_end(struct nlmsghdr *msg, struct rtattr *nest)
{
	nest->rta_len = (uint8_t *)nlmsg_tail(msg) - (uint8_t *)nest;
}

/* Open RTNL socket */
static int ovpn_rt_socket(ovpn_ctx_t *ovpn)
{
	const char *args_name = ovpn->args_name;
	struct timeval rcvtimeo = { RT_RCVTIMEO_SEC, 0 };
	int sndbuf = RT_SNDBUF_SIZE;
	int rcvbuf = RT_RCVBUF_SIZE;
	int fd;

	fd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
	if (fd < 0) {
		pr_dbg("%s: netlink socket failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return fd;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf,
		       sizeof(sndbuf)) < 0) {
		pr_dbg("%s: setsockopt SO_SNDBUF failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		(void)close(fd);
		return -1;
	}

	if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvbuf,
		       sizeof(rcvbuf)) < 0) {
		pr_dbg("%s: setsockopt SO_RCVBUF failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		(void)close(fd);
		return -1;
	}

	/*
	 * Bound the wait for a reply. This is what turns a lost reply into an
	 * error the caller can act on, rather than a worker that never returns.
	 */
	if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rcvtimeo,
		       sizeof(rcvtimeo)) < 0) {
		pr_dbg("%s: setsockopt SO_RCVTIMEO failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		(void)close(fd);
		return -1;
	}

	return fd;
}

/* Bind socket to Netlink subsystem */
static int ovpn_rt_bind(
	ovpn_ctx_t *ovpn,
	const int fd,
	const uint32_t groups)
{
	struct sockaddr_nl local = { 0 };
	socklen_t addr_len;
	const char *args_name = ovpn->args_name;

	local.nl_family = AF_NETLINK;
	local.nl_groups = groups;

	if (bind(fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
		pr_dbg("%s: netlink socket bind failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -errno;
	}

	addr_len = sizeof(local);
	if (getsockname(fd, (struct sockaddr *)&local, &addr_len) < 0) {
		pr_dbg("%s: getsockname failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -errno;
	}

	if (addr_len != sizeof(local)) {
		pr_dbg("%s: wrong address length %d\n", args_name, addr_len);
		return -EINVAL;
	}

	if (local.nl_family != AF_NETLINK) {
		pr_dbg("%s: wrong address family %d\n", args_name, local.nl_family);
		return -EINVAL;
	}

	return 0;
}

/* Send Netlink message and run callback on reply (if specified) */
static int ovpn_rt_send(
	ovpn_ctx_t *ovpn,
	struct nlmsghdr *payload,
	const pid_t peer,
	const unsigned int groups,
	ovpn_parse_reply_cb cb,
	void *arg_cb)
{
	int len;
	int rem_len;
	int fd;
	int ret;
	int rcv_len;
	struct sockaddr_nl nladdr = { 0 };
	struct nlmsgerr *err;
	struct nlmsghdr *h;
	const char *args_name = ovpn->args_name;
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

	/* no reply parser, so ask for a plain ack */
	if (!cb)
		payload->nlmsg_flags |= NLM_F_ACK;

	fd = ovpn_rt_socket(ovpn);
	if (fd < 0) {
		pr_dbg("%s: open socket failed\n", args_name);
		return -errno;
	}

	ret = ovpn_rt_bind(ovpn, fd, 0);
	if (ret < 0) {
		pr_dbg("%s: bind socket failed\n", args_name);
		ret = -errno;
		goto out;
	}

	ret = sendmsg(fd, &nlmsg, 0);
	if (ret < 0) {
		pr_dbg("%s: sendmsg() failed\n", args_name);
		ret = -errno;
		goto out;
	}

	/* prepare buffer to store RTNL replies */
	(void)shim_memset(buf, 0, sizeof(buf));
	iov.iov_base = buf;

	while (1) {
		/*
		 * iov_len is modified by recvmsg(), therefore has to be initialized before
		 * using it again
		 */
		iov.iov_len = sizeof(buf);
		rcv_len = recvmsg(fd, &nlmsg, 0);
		if (rcv_len < 0) {
			/*
			 * With SO_RCVTIMEO set, EAGAIN means the deadline
			 * expired rather than "try again": the reply is not
			 * coming, so report it instead of asking for it again
			 * forever.
			 */
			if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
				pr_dbg("%s: no netlink reply within %d seconds\n",
					args_name, RT_RCVTIMEO_SEC);
				ret = -ETIMEDOUT;
				goto out;
			}
			if (errno == EINTR) {
				/*
				 * Retrying is right for a signal, but not once
				 * the run is winding down: SIGALRM would then
				 * be swallowed on every iteration and the
				 * worker would never notice it has to stop.
				 */
				if (UNLIKELY(!stress_continue_flag())) {
					ret = -EINTR;
					goto out;
				}
				continue;
			}
			pr_dbg("%s: recvmsg() failed\n", args_name);
			ret = -errno;
			goto out;
		}

		if (rcv_len == 0) {
			pr_dbg("%s: socket reached unexpected EOF\n", args_name);
			ret = -EIO;
			goto out;
		}

		if (nlmsg.msg_namelen != sizeof(nladdr)) {
			pr_dbg("%s: sender address length: %u (expected %zu)\n",
				args_name, nlmsg.msg_namelen, sizeof(nladdr));
			ret = -EIO;
			goto out;
		}

		h = (struct nlmsghdr *)(uintptr_t)buf;
		while (rcv_len >= (int)sizeof(*h)) {
			len = h->nlmsg_len;
			rem_len = len - sizeof(*h);

			if (rem_len < 0 || len > rcv_len) {
				if (nlmsg.msg_flags & MSG_TRUNC) {
					pr_dbg("%s: truncated message\n", args_name);
					ret = -EIO;
					goto out;
				}
				pr_dbg("%s: malformed message: len=%d\n", args_name, len);
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
					pr_dbg("%s: error, truncated\n", args_name);
					ret = -EIO;
					goto out;
				}

				if (err->error) {
#if defined(DEBUG_MORE)
					pr_dbg("%s: debug (%d) %s\n",
						args_name, err->error, strerror(-err->error));
#endif
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
				pr_dbg("%s: unexpected reply\n", args_name);
			}

			rcv_len -= NLMSG_ALIGN(len);
			h = (struct nlmsghdr *)(uintptr_t)((uint8_t *)h + NLMSG_ALIGN(len));
		}

		if (nlmsg.msg_flags & MSG_TRUNC) {
			pr_dbg("%s: message truncated\n", args_name);
			continue;
		}

		if (rcv_len) {
			pr_dbg("%s: %d not parsed bytes\n", args_name, rcv_len);
			ret = -1;
			goto out;
		}
	}
out:
	(void)close(fd);

	return ret;
}

static int ovpn_socket(
	ovpn_ctx_t *ovpn,
	sa_family_t family,
	const int proto)
{
	struct sockaddr_storage local_sock = { 0 };
	struct sockaddr_in6 *in6;
	struct sockaddr_in *in;
	const char *args_name = ovpn->args_name;
	int ret;
	int s;
	int sock_type;
	size_t sock_len;
	int opt = 1;

	if (proto == IPPROTO_UDP)
		sock_type = SOCK_DGRAM;
	else if (proto == IPPROTO_TCP)
		sock_type = SOCK_STREAM;
	else
		return -EINVAL;

	s = socket(family, sock_type, 0);
	if (s < 0) {
		pr_err("%s: cannot create socket: errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -1;
	}

	switch (family) {
	case AF_INET:
		in = (struct sockaddr_in *)&local_sock;
		in->sin_family = family;
		in->sin_port = htons(ovpn->lport);
		in->sin_addr.s_addr = htonl(INADDR_ANY);
		sock_len = sizeof(*in);
		break;
	case AF_INET6:
		in6 = (struct sockaddr_in6 *)&local_sock;
		in6->sin6_family = family;
		in6->sin6_port = htons(ovpn->lport);
		in6->sin6_addr = in6addr_any;
		sock_len = sizeof(*in6);
		break;
	default:
		(void)close(s);
		return -1;
	}

	ret = setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	if (ret < 0) {
		pr_err("%s: setsockopt SO_REUSEADDR failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		goto err_socket;
	}

	ret = setsockopt(s, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(opt));
	if (ret < 0) {
		pr_err("%s: setsockopt SO_REUSEPORT failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		goto err_socket;
	}

	if (family == AF_INET6) {
		opt = 0;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &opt,
			       sizeof(opt))) {
			pr_err("%s: setsockopt IPV6_V6ONLY failed, errno=%d %s\n",
				args_name, errno, strerror(errno));
			goto err_socket;
		}
	}

	ret = bind(s, (struct sockaddr *)&local_sock, sock_len);
	if (ret < 0) {
		pr_err("%s: bind failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		goto err_socket;
	}

	ovpn->socket = s;
	ovpn->sa_family = family;
	return 0;

err_socket:
	(void)close(s);
	return -1;
}

static int ovpn_new_iface(ovpn_ctx_t *ovpn)
{
	struct rtattr *linkinfo, *data;
	ovpn_link_req_t req = { 0 };
	int ret = -1;

	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	req.n.nlmsg_type = RTM_NEWLINK;

	if (ovpn_addattr(ovpn, &req.n, sizeof(req), IFLA_IFNAME, ovpn->ifname,
			 strlen(ovpn->ifname) + 1) < 0)
		goto err;

	linkinfo = ovpn_nest_start(ovpn, &req.n, sizeof(req), IFLA_LINKINFO);
	if (!linkinfo)
		goto err;

	if (ovpn_addattr(ovpn, &req.n, sizeof(req), IFLA_INFO_KIND, OVPN_FAMILY_NAME,
			 strlen(OVPN_FAMILY_NAME) + 1) < 0)
		goto err;

	if (ovpn->mode_set) {
		data = ovpn_nest_start(ovpn, &req.n, sizeof(req), IFLA_INFO_DATA);
		if (!data)
			goto err;

		if (ovpn_addattr(ovpn, &req.n, sizeof(req), IFLA_OVPN_MODE,
				 &ovpn->mode, sizeof(uint8_t)) < 0)
			goto err;

		ovpn_nest_end(&req.n, data);
	}

	ovpn_nest_end(&req.n, linkinfo);

	req.i.ifi_family = AF_PACKET;

	ret = ovpn_rt_send(ovpn, &req.n, 0, 0, NULL, NULL);
err:
	return ret;
}

static nl_ctx_t *nl_ctx_alloc_flags(
	ovpn_ctx_t *ovpn,
	const int cmd,
	const int flags)
{
	nl_ctx_t *ctx;
	int err;
	int ret;
	const char *args_name = ovpn->args_name;

	ctx = calloc(1, sizeof(*ctx));
	if (!ctx)
		return NULL;

	ctx->ovpn = ovpn;
	ctx->nl_sock = nl_socket_alloc();
	if (!ctx->nl_sock) {
		pr_err("%s: cannot allocate netlink socket\n", args_name);
		goto err_free;
	}

	nl_socket_set_buffer_size(ctx->nl_sock, 8192, 8192);

	ret = genl_connect(ctx->nl_sock);
	if (ret) {
		pr_dbg("%s: cannot connect to generic netlink: %s\n",
			args_name, nl_geterror(ret));
		goto err_sock;
	}

	/* enable Extended ACK for detailed error reporting; ignore failure */
	err = 1;
	(void)setsockopt(nl_socket_get_fd(ctx->nl_sock), SOL_NETLINK, NETLINK_EXT_ACK,
		   &err, sizeof(err));

	ctx->ovpn_dco_id = genl_ctrl_resolve(ctx->nl_sock, OVPN_FAMILY_NAME);
	if (ctx->ovpn_dco_id < 0) {
#if defined(DEBUG_MORE)
		pr_dbg("%s: cannot find ovpn_dco netlink component, %d\n",
			args_name, ctx->ovpn_dco_id);
#endif
		goto err_sock;
	}

	ctx->nl_msg = nlmsg_alloc();
	if (!ctx->nl_msg) {
		pr_err("%s: cannot allocate netlink message\n", args_name);
		goto err_sock;
	}

	ctx->nl_cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!ctx->nl_cb) {
		pr_err("%s: failed to allocate netlink callback\n", args_name);
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

static nl_ctx_t *nl_ctx_alloc(ovpn_ctx_t *ovpn, const int cmd)
{
	return nl_ctx_alloc_flags(ovpn, cmd, 0);
}

static int ovpn_nl_cb_finish(struct nl_msg *msg, void *arg)
{
	nl_ctx_t *ctx = (nl_ctx_t *)arg;

	(void)msg;

	ctx->nl_status = 0;
	return NL_SKIP;
}

static int ovpn_nl_cb_ack(struct nl_msg *msg, void *arg)
{
	nl_ctx_t *ctx = (nl_ctx_t *)arg;

	(void)msg;

	ctx->nl_status = 0;
	return NL_STOP;
}

static int ovpn_nl_recvmsgs(
	ovpn_ctx_t *ovpn,
	nl_ctx_t *ctx)
{
	int ret;
	const char *args_name = ovpn->args_name;

	ret = nl_recvmsgs(ctx->nl_sock, ctx->nl_cb);

	if (ovpn->expect_failure)
		return ret;

	switch (ret) {
	case -NLE_INTR:
		pr_dbg("%s: netlink received interrupt due to signal, ignoring\n", args_name);
		break;
	case -NLE_NOMEM:
		pr_dbg("%s: netlink out of memory error\n", args_name);
		break;
	case -NLE_AGAIN:
		pr_dbg("%s: netlink reports blocking read - aborting wait\n", args_name);
		break;
	default:
		if (ret)
			pr_dbg("%s: netlink reports error=%d (%s)\n", args_name,
				ret, nl_geterror(-ret));
		break;
	}

	return ret;
}

static int ovpn_nl_cb_error(
	struct sockaddr_nl *nla,
	struct nlmsgerr *err,
	void *arg)
{
	struct nlmsghdr *nlh = (struct nlmsghdr *)err - 1;
	struct nlattr *tb_msg[NLMSGERR_ATTR_MAX + 1];
	int len = nlh->nlmsg_len;
	struct nlattr *attrs;
	nl_ctx_t *ctx = (nl_ctx_t *)arg;
	int ack_len = sizeof(*nlh) + sizeof(int) + sizeof(*nlh);

	(void)nla;

	ctx->nl_status = err->error;

	/* an expected failure still has to be reported back, just not logged */
	if (ctx->ovpn && ctx->ovpn->expect_failure)
		return NL_STOP;

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
		pr_dbg("ovpn: kernel error %*s\n", len,
			(char *)nla_data(tb_msg[NLMSGERR_ATTR_MSG]));
	}

#ifdef NLMSGERR_ATTR_MISS_NEST
	if (tb_msg[NLMSGERR_ATTR_MISS_NEST]) {
		pr_dbg("ovpn: missing required nesting type %u\n",
			nla_get_u32(tb_msg[NLMSGERR_ATTR_MISS_NEST]));
	}
#endif

#ifdef NLMSGERR_ATTR_MISS_TYPE
	if (tb_msg[NLMSGERR_ATTR_MISS_TYPE]) {
		pr_dbg("ovpn: missing required attribute type %u\n",
			nla_get_u32(tb_msg[NLMSGERR_ATTR_MISS_TYPE]));
	}
#endif

	return NL_STOP;
}

static int ovpn_nl_msg_send(
	ovpn_ctx_t *ovpn,
	nl_ctx_t *ctx,
	ovpn_nl_cb cb)
{
	ctx->nl_status = 1;

	nl_cb_err(ctx->nl_cb, NL_CB_CUSTOM, ovpn_nl_cb_error, ctx);
	nl_cb_set(ctx->nl_cb, NL_CB_FINISH, NL_CB_CUSTOM, ovpn_nl_cb_finish, ctx);
	nl_cb_set(ctx->nl_cb, NL_CB_ACK, NL_CB_CUSTOM, ovpn_nl_cb_ack, ctx);

	if (cb)
		nl_cb_set(ctx->nl_cb, NL_CB_VALID, NL_CB_CUSTOM, cb, ctx);

	nl_send_auto_complete(ctx->nl_sock, ctx->nl_msg);

	while ((ctx->nl_status == 1) && stress_continue_flag())
		ovpn_nl_recvmsgs(ovpn, ctx);

	if ((ctx->nl_status < 0) && !ovpn->expect_failure)
		pr_dbg("%s: failed to send netlink message, errno=%d (%s)\n",
			ovpn->args_name, ctx->nl_status, strerror(-ctx->nl_status));

	return ctx->nl_status;
}

static void nl_ctx_free(nl_ctx_t *ctx)
{
	if (!ctx)
		return;

	nl_socket_free(ctx->nl_sock);
	nlmsg_free(ctx->nl_msg);
	nl_cb_put(ctx->nl_cb);
	free(ctx);
}

static int ovpn_new_peer(ovpn_ctx_t *ovpn, const bool is_tcp)
{
	struct nlattr *attr;
	nl_ctx_t *ctx;
	int ret = -1;
	const char *args_name = ovpn->args_name;

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
			pr_dbg("%s: invalid family for remote socket address\n", args_name);
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
			pr_dbg("%s: invalid family for peer address\n", args_name);
			goto nla_put_failure;
		}
	}

	nla_nest_end(ctx->nl_msg, attr);

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_parse_remote(
	ovpn_ctx_t *ovpn,
	const char *host,
	const char *service,
	const char *vpnip)
{
	int ret;
	const char *args_name = ovpn->args_name;
	struct addrinfo *result;
	struct addrinfo hints = {
		.ai_family = ovpn->sa_family,
		.ai_socktype = SOCK_DGRAM,
		.ai_protocol = IPPROTO_UDP
	};

	if (host) {
		ret = getaddrinfo(host, service, &hints, &result);
		if (ret) {
#if defined(DEBUG_MORE)
			pr_dbg("%s: getaddrinfo failed on remote, error %s\n",
				args_name, gai_strerror(ret));
#endif
			return -1;
		}

		if (!(result->ai_family == AF_INET &&
		      result->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    !(result->ai_family == AF_INET6 &&
		      result->ai_addrlen == sizeof(struct sockaddr_in6))) {
			freeaddrinfo(result);
			return -EINVAL;
		}

		(void)shim_memcpy(&ovpn->remote, result->ai_addr, result->ai_addrlen);
		freeaddrinfo(result);
	}

	if (vpnip) {
		ret = getaddrinfo(vpnip, NULL, &hints, &result);
		if (ret) {
			pr_dbg("%s: getaddrinfo failed on vpnip, error %s\n",
				args_name, gai_strerror(ret));
			return -1;
		}

		if (!(result->ai_family == AF_INET &&
		      result->ai_addrlen == sizeof(struct sockaddr_in)) &&
		    !(result->ai_family == AF_INET6 &&
		      result->ai_addrlen == sizeof(struct sockaddr_in6))) {
			freeaddrinfo(result);
			return -EINVAL;
		}

		(void)shim_memcpy(&ovpn->peer_ip, result->ai_addr, result->ai_addrlen);
		ovpn->sa_family = result->ai_family;
		ovpn->peer_ip_set = true;
		freeaddrinfo(result);
	}

	return 0;
}

static int ovpn_parse_new_peer(
	ovpn_ctx_t *ovpn,
	const int peer_id,
	const char *raddr,
	const char *rport,
	const char *vpnip)
{
	ovpn->peer_id = peer_id;
	if (ovpn->peer_id > PEER_ID_UNDEF) {
		pr_dbg("%s: peer ID value out of range\n", ovpn->args_name);
		return -1;
	}

	return ovpn_parse_remote(ovpn, raddr, rport, vpnip);
}

static int ovpn_connect(ovpn_ctx_t *ovpn)
{
	socklen_t socklen;
	int s;
	int ret;
	int flags;
	fd_set wfds;
	struct timeval tv = { .tv_sec = 2, .tv_usec = 0 };
	const char *args_name = ovpn->args_name;

	switch (ovpn->remote.in4.sin_family) {
	case AF_INET:
		socklen = sizeof(struct sockaddr_in);
		break;
	case AF_INET6:
		socklen = sizeof(struct sockaddr_in6);
		break;
	default:
		ret = -EOPNOTSUPP;
		goto err_ret;
	}

	s = socket(ovpn->remote.in4.sin_family, SOCK_STREAM, 0);
	if (s < 0) {
		pr_err("%s: socket failed, errno=%d (%s)\n",
			args_name,
			errno, strerror(errno));
		return -1;
	}

	flags = fcntl(s, F_GETFL, 0);
	if (flags < 0 || fcntl(s, F_SETFL, flags | O_NONBLOCK) < 0) {
		pr_err("%s: fcntl failed, errno=%d  (%s)\n",
			args_name, errno, strerror(errno));
		ret = -1;
		goto err;
	}

	ret = connect(s, (struct sockaddr *)&ovpn->remote, socklen);
	if (ret < 0 && errno != EINPROGRESS) {
		pr_dbg("%s: connect failed, errno=%d (%s(\n",
			args_name, errno, strerror(errno));
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
	(void)close(s);
err_ret:
	return ret;
}

static int ovpn_new_key(ovpn_ctx_t *ovpn)
{
	struct nlattr *keyconf, *key_dir;
	nl_ctx_t *ctx;
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

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_send_tcp_data(const int socket)
{
	uint16_t len = htons(1000);
	uint8_t buf[1002];
	int ret;

	(void)shim_memcpy(buf, &len, sizeof(len));
	(void)shim_memset(buf + sizeof(len), 0x86, sizeof(buf) - sizeof(len));

	ret = send(socket, buf, sizeof(buf), MSG_NOSIGNAL);

	return ret > 0 ? 0 : ret;
}

static inline int ovpn_udp_socket(ovpn_ctx_t *ctx, sa_family_t family)
{
	return ovpn_socket(ctx, family, IPPROTO_UDP);
}

static int ovpn_set_peer(ovpn_ctx_t *ovpn)
{
	struct nlattr *attr;
	nl_ctx_t *ctx;
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

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_del_peer(ovpn_ctx_t *ovpn)
{
	struct nlattr *attr;
	nl_ctx_t *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_PEER_DEL);
	if (!ctx)
		return -ENOMEM;

	attr = nla_nest_start(ctx->nl_msg, OVPN_A_PEER);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_PEER_ID, ovpn->peer_id);
	nla_nest_end(ctx->nl_msg, attr);

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_swap_keys(ovpn_ctx_t *ovpn)
{
	struct nlattr *kc;
	nl_ctx_t *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_SWAP);
	if (!ctx)
		return -ENOMEM;

	kc = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	nla_nest_end(ctx->nl_msg, kc);

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_del_key(ovpn_ctx_t *ovpn)
{
	struct nlattr *keyconf;
	nl_ctx_t *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_DEL);
	if (!ctx)
		return -ENOMEM;

	keyconf = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_SLOT, ovpn->key_slot);
	nla_nest_end(ctx->nl_msg, keyconf);

	ret = ovpn_nl_msg_send(ovpn, ctx, NULL);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_handle_peer(struct nl_msg *msg, void *arg)
{
	struct nlattr *pattrs[OVPN_A_PEER_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *attrs[OVPN_A_MAX + 1];
	const nl_ctx_t *ctx = (const nl_ctx_t *)arg;

	nla_parse(attrs, OVPN_A_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!attrs[OVPN_A_PEER])
		return NL_SKIP;

	nla_parse(pattrs, OVPN_A_PEER_MAX, nla_data(attrs[OVPN_A_PEER]),
		  nla_len(attrs[OVPN_A_PEER]), NULL);

	/*
	 * lift the kernel's plaintext-side byte counters into the context, so
	 * the tunnel mode can report measured rather than estimated traffic.
	 * ovpn_nla_get_uint() copes with the u32/u64 width difference across
	 * libnl versions. The attributes are only present on a module that
	 * exports them, hence the validity flag.
	 */
	if (ctx && ctx->ovpn) {
		struct ovpn_ctx *ovpn = ctx->ovpn;

		if (pattrs[OVPN_A_PEER_VPN_RX_BYTES]) {
			ovpn->peer_vpn_rx_bytes =
				ovpn_nla_get_uint(pattrs[OVPN_A_PEER_VPN_RX_BYTES]);
			ovpn->peer_stats_valid = true;
		}
		if (pattrs[OVPN_A_PEER_VPN_TX_BYTES]) {
			ovpn->peer_vpn_tx_bytes =
				ovpn_nla_get_uint(pattrs[OVPN_A_PEER_VPN_TX_BYTES]);
			ovpn->peer_stats_valid = true;
		}
	}

	return NL_SKIP;
}

static int ovpn_get_peer(ovpn_ctx_t *ovpn)
{
	int flags = 0;
	int ret = -1;
	struct nlattr *attr;
	nl_ctx_t *ctx;

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

	ret = ovpn_nl_msg_send(ovpn, ctx, ovpn_handle_peer);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_handle_key(struct nl_msg *msg, void *arg)
{
	struct nlattr *kattrs[OVPN_A_KEYCONF_MAX + 1];
	struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *attrs[OVPN_A_MAX + 1];

	(void)arg;

	nla_parse(attrs, OVPN_A_MAX, genlmsg_attrdata(gnlh, 0),
		  genlmsg_attrlen(gnlh, 0), NULL);

	if (!attrs[OVPN_A_KEYCONF])
		return NL_SKIP;

	nla_parse(kattrs, OVPN_A_KEYCONF_MAX, nla_data(attrs[OVPN_A_KEYCONF]),
		  nla_len(attrs[OVPN_A_KEYCONF]), NULL);

	return NL_SKIP;
}

static int ovpn_get_key(ovpn_ctx_t *ovpn)
{
	struct nlattr *keyconf;
	nl_ctx_t *ctx;
	int ret = -1;

	ctx = nl_ctx_alloc(ovpn, OVPN_CMD_KEY_GET);
	if (!ctx)
		return -ENOMEM;

	keyconf = nla_nest_start(ctx->nl_msg, OVPN_A_KEYCONF);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_PEER_ID, ovpn->peer_id);
	NLA_PUT_U32(ctx->nl_msg, OVPN_A_KEYCONF_SLOT, ovpn->key_slot);
	nla_nest_end(ctx->nl_msg, keyconf);

	ret = ovpn_nl_msg_send(ovpn, ctx, ovpn_handle_key);
nla_put_failure:
	nl_ctx_free(ctx);
	return ret;
}

static int ovpn_run_cmd(ovpn_ctx_t *ovpn)
{
	int ret = 0;
	const char *args_name = ovpn->args_name;

	switch (ovpn->cmd) {
	case CMD_NEW_IFACE:
		ret = ovpn_new_iface(ovpn);
		break;
	case CMD_CONNECT:
		ret = ovpn_connect(ovpn);
		if (ret < 0) {
#if defined(DEBUG_MORE)
			pr_dbg("%s: cannot connect TCP socket\n", args_name);
#endif
			return ret;
		}

		ret = ovpn_new_peer(ovpn, true);
		if (ret < 0) {
#if defined(DEBUG_MORE)
			pr_dbg("%s: cannot add peer to VPN\n", args_name);
#endif
			(void)close(ovpn->socket);
			return ret;
		}

		if (ovpn->cipher != OVPN_CIPHER_ALG_NONE) {
			ret = ovpn_new_key(ovpn);
			if (ret < 0) {
				pr_dbg("%s: cannot set key\n", args_name);
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

static void ovpn_ctx_reset(ovpn_ctx_t *ovpn)
{
	const char *args_name = ovpn->args_name;

	if (ovpn->socket >= 0)
		(void)close(ovpn->socket);

	(void)shim_memset(ovpn, 0, sizeof(*ovpn));

	ovpn->args_name = args_name;
	ovpn->socket = -1;

	(void)shim_strscpy(ovpn->ifname, "tun0", IFNAMSIZ);
	ovpn->ifindex = if_nametoindex(ovpn->ifname);

	ovpn->sa_family = AF_INET;
	ovpn->cipher = OVPN_CIPHER_ALG_NONE;
}

static int build_new_iface(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_NEW_IFACE;
	ovpn->mode = stress_mwc1() ? SHIM_OVPN_MODE_P2P : SHIM_OVPN_MODE_MP;
	ovpn->mode_set = true;

	return 0;
}

static int ovpn_generate_key(ovpn_ctx_t *ovpn)
{
	const char *args_name = ovpn->args_name;

	if (getrandom(ovpn->key_enc, KEY_LEN, 0) != KEY_LEN) {
		pr_err("%s: getrandom(key_enc) failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -1;
	}

	if (getrandom(ovpn->key_dec, KEY_LEN, 0) != KEY_LEN) {
		pr_err("%s: getrandom(key_dec) failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -1;
	}

	if (getrandom(ovpn->nonce, NONCE_LEN, 0) != NONCE_LEN) {
		pr_err("%s: getrandom(nonce) failed, errno=%d (%s)\n",
			args_name, errno, strerror(errno));
		return -1;
	}

	return 0;
}

static void ovpn_rand_addr_port(
	char *addr,
	const size_t alen,
	char *port,
	const size_t plen)
{
	(void)snprintf(addr, alen, "10.%u.%u.%u",
		stress_mwc8() + 1, stress_mwc8(), stress_mwc8() + 1);
	(void)snprintf(port, plen, "%u",
		(stress_mwc16() % 64511) + 1024);
}

static int build_connect(ovpn_ctx_t *ovpn)
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
	ovpn->key_dir  = SHIM_KEY_DIR_OUT;

	return ovpn_generate_key(ovpn);
}

static int build_new_peer(ovpn_ctx_t *ovpn)
{
	char addr[INET_ADDRSTRLEN], port[6];

	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_NEW_PEER;
	ovpn->lport = 1194;

	ovpn_rand_addr_port(addr, sizeof(addr), port, sizeof(port));
	return ovpn_parse_new_peer(ovpn, stress_mwc32() % 10, addr, port, NULL);
}

static int build_set_peer(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_SET_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->keepalive_interval = 10;
	ovpn->keepalive_timeout  = 60;

	return 0;
}

static int build_del_peer(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_DEL_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int build_get_peer(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_GET_PEER;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int build_new_key(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_NEW_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;
	ovpn->key_id = 0;
	ovpn->cipher = OVPN_CIPHER_ALG_AES_GCM;
	ovpn->key_dir = SHIM_KEY_DIR_OUT;

	return ovpn_generate_key(ovpn);
}

static int build_del_key(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_DEL_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;

	return 0;
}

static int build_get_key(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);

	ovpn->cmd = CMD_GET_KEY;
	ovpn->peer_id = stress_mwc32() % 10;
	ovpn->key_slot = OVPN_KEY_SLOT_PRIMARY;

	return 0;
}

static int build_swap_keys(ovpn_ctx_t *ovpn)
{
	ovpn_ctx_reset(ovpn);
	ovpn->cmd = CMD_SWAP_KEYS;
	ovpn->peer_id = stress_mwc32() % 10;
	return 0;
}

static int ovpn_autofill_args(ovpn_ctx_t *ovpn)
{
	switch (ovpn->cmd) {
	case CMD_NEW_IFACE:
		return build_new_iface(ovpn);
	case CMD_CONNECT:
		return build_connect(ovpn);
	case CMD_NEW_PEER:
		return build_new_peer(ovpn);
	case CMD_SET_PEER:
		return build_set_peer(ovpn);
	case CMD_DEL_PEER:
		return build_del_peer(ovpn);
	case CMD_GET_PEER:
		return build_get_peer(ovpn);
	case CMD_NEW_KEY:
		return build_new_key(ovpn);
	case CMD_DEL_KEY:
		return build_del_key(ovpn);
	case CMD_GET_KEY:
		return build_get_key(ovpn);
	case CMD_SWAP_KEYS:
		return build_swap_keys(ovpn);
	default:
		return 0;
	}
}

/*
 *  ============================================================
 *  --ovpn-tunnel mode: build a real two-endpoint DCO tunnel
 *
 *  The stressor instance first unshares its own network namespace,
 *  so every interface it creates is private to the instance and is
 *  reclaimed by the kernel when the instance exits; nothing is ever
 *  added to the host network namespace. Inside it, each cycle forks
 *  two children that unshare a network namespace each, joined by a
 *  veth pair used as the transport underlay (UDP or TCP, picked at
 *  random per cycle). An ovpn interface is created in each child
 *  netns with a fixed shared key, then the client injects traffic
 *  into its tunnel interface so the kernel actually ENCRYPTs and
 *  the server DECRYPTs it:
 *
 *    client netns                     server netns
 *    veth-c 172.16.0.2  ── underlay ── veth-s 172.16.0.1
 *    tun0   10.8.0.2                   tun0   10.8.0.1
 *    inject → 10.8.0.1 → ENCRYPT → veth → DECRYPT → tun0
 *  ============================================================
 */
#define OVPN_TUN_PORT		(1194)
#define OVPN_TUN_DATA_PORT	(5555)	/* inner UDP port for injected data */
#define OVPN_TUN_PACKETS	(16)
#define OVPN_TUN_KEY_BYTE	(0x5a)
/*
 * TCP underlay connect budget: few attempts, each waiting a long time.
 * The wait has to be generous rather than the attempt count: abandoning a
 * connect() that is merely slow leaves behind a connection the server has
 * already accepted, and the module then refuses that dead socket with
 * EINVAL. Retrying quickly is only right when nothing is listening yet,
 * and that case reports itself immediately through SO_ERROR.
 */
#define OVPN_TUN_CONNECT_TRIES	(5)
#define OVPN_TUN_CONNECT_USEC	(1000000)
/*
 * the server has to stay in accept() for at least as long as the client
 * keeps retrying, otherwise it gives up first and the cycle is wasted
 */
#define OVPN_TUN_ACCEPT_USEC	((OVPN_TUN_CONNECT_TRIES * OVPN_TUN_CONNECT_USEC) + 500000)
/*
 * consecutive soft failures tolerated before concluding there is nothing to
 * do. Only transient causes reach this: a hard failure stops on the first
 * attempt, so this can afford to be forgiving of a contended start.
 */
#define OVPN_TUN_MAX_FAILS	(16)

/* tokens exchanged over the parent <-> child sync pipes */
#define OVPN_TUN_READY		'R'	/* child unshared its netns */
#define OVPN_TUN_NO_NETNS	'F'	/* child could not unshare */
#define OVPN_TUN_GO		'G'	/* parent migrated the veth end */

static const uint8_t ovpn_tun_ul_srv[4] = { 172, 16, 0, 1 };	/* underlay */
static const uint8_t ovpn_tun_ul_cli[4] = { 172, 16, 0, 2 };
static const uint8_t ovpn_tun_in_srv[4] = { 10, 8, 0, 1 };	/* inner/vpn */
static const uint8_t ovpn_tun_in_cli[4] = { 10, 8, 0, 2 };

/*
 *  ovpn_tun_phantom_id()
 *	peer id for a phantom. Mostly a small range, so ids collide and the
 *	table is genuinely reused rather than just grown, but one time in
 *	four an edge of what the uapi allows: zero, the reserved undefined
 *	value - which also means "dump everything" on a get - the value below
 *	it, and something no daemon would ever pick.
 */
static uint32_t ovpn_tun_phantom_id(void)
{
	switch (stress_mwc8() % 16) {
	case 0:
		return 0;
	case 1:
		return PEER_ID_UNDEF;
	case 2:
		return PEER_ID_UNDEF - 1;
	case 3:
		return stress_mwc32() & PEER_ID_UNDEF;
	default:
		return 2 + (stress_mwc8() % 62);
	}
}

/*
 *  ovpn_tun_cipher()
 *	a cipher for a key. NONE is in the mix on purpose: with no AEAD any
 *	bytes "decrypt" successfully and go straight to the module's inner
 *	protocol check, which is a path a well behaved daemon never takes.
 */
static enum ovpn_cipher_alg ovpn_tun_cipher(void)
{
	switch (stress_mwc8() % 3) {
	case 0:
		return OVPN_CIPHER_ALG_AES_GCM;
	case 1:
		return OVPN_CIPHER_ALG_CHACHA20_POLY1305;
	default:
		return OVPN_CIPHER_ALG_NONE;
	}
}

/*
 *  ovpn_tun_phantom_vpn_ip()
 *	random inner address for a phantom peer, over the whole 10.8.0.0/16.
 *	The real peer's own address is deliberately reachable here: a phantom
 *	claiming it makes the module insert a duplicate VPN address and
 *	displace the traffic-carrying peer, which exercises the MP peer table
 *	where it is most likely to go wrong. The cost is that such a cycle
 *	stops delivering and reports no traffic, which is the right trade for
 *	a stressor.
 */
static void ovpn_tun_phantom_vpn_ip(uint8_t vpn[4])
{
	vpn[0] = 10;
	vpn[1] = 8;
	vpn[2] = stress_mwc8();
	vpn[3] = stress_mwc8();
}

/*
 * shared counters in an anonymous shared mapping, so the forked children
 * accumulate into them and the instance reports them.
 *
 * The byte counters are the kernel's own per-peer plaintext-side totals,
 * read back with OVPN_CMD_PEER_GET when a cycle winds down, split by
 * direction and by the transport of the cycle that produced them:
 *   tx = VPN_TX_BYTES of the client's peer  (fed to the crypto path)
 *   rx = VPN_RX_BYTES of the server's peer  (decrypted and delivered)
 *
 * OPS counts injection bursts, and is folded into the bogo-op counter by
 * the instance rather than by the children, see ovpn_tunnel_cycle().
 */
enum {
	OVPN_CTR_TX_UDP = 0,
	OVPN_CTR_TX_TCP,
	OVPN_CTR_RX_UDP,
	OVPN_CTR_RX_TCP,
	OVPN_CTR_OPS,
	OVPN_CTR_FAIL,
	OVPN_CTR_MAX
};

/*
 * Why the last cycle failed. Every failure path used to collapse into an
 * undifferentiated EXIT_NO_RESOURCE, which left the instance guessing at a
 * cause when it gave up. A hard reason will not improve by retrying, so one
 * is enough to stop; a soft one is timing or contention and the next cycle
 * may well succeed.
 */
typedef enum {
	OVPN_FAIL_NONE = 0,
	OVPN_FAIL_NETNS,	/* hard */
	OVPN_FAIL_IFACE,	/* hard */
	OVPN_FAIL_PEER,		/* soft */
	OVPN_FAIL_ADDR,		/* soft */
	OVPN_FAIL_TCP,		/* soft */
	OVPN_FAIL_RESOURCE,	/* soft */
	OVPN_FAIL_MAX
} ovpn_fail_t;

static const char * const ovpn_fail_reason[] = {
	"no failure",
	"cannot unshare a network namespace, CAP_SYS_ADMIN is needed",
	"cannot create an ovpn interface, the ovpn module may be unavailable",
	"the ovpn module rejected the peer or its key",
	"cannot configure the veth or tunnel addresses",
	"the TCP underlay handshake did not complete",
	"out of processes, pipes or network devices",
};
static uint64_t *ovpn_bytes;		/* array of OVPN_CTR_MAX counters */

/*
 *  ovpn_fail_set()
 *	record why this cycle failed, for the instance to report if it ends
 *	up giving up. Written by whichever process hit the failure, so it
 *	lives in the shared mapping; last writer wins, which is fine for a
 *	diagnostic.
 */
static void ovpn_fail_set(const ovpn_fail_t reason)
{
	if (ovpn_bytes)
		__atomic_store_n(&ovpn_bytes[OVPN_CTR_FAIL],
			(uint64_t)reason, __ATOMIC_RELAXED);
}

/*
 *  ovpn_fail_str()
 *	describe a recorded failure reason
 */
static const char *ovpn_fail_str(const uint64_t reason)
{
	/* the value comes out of shared memory, so bound it */
	return (reason < OVPN_FAIL_MAX) ?
		ovpn_fail_reason[reason] : "an unrecognised failure";
}

/*
 *  ovpn_fail_is_hard()
 *	true for the failures that retrying cannot fix: no privileges or no
 *	module
 */
static bool ovpn_fail_is_hard(const uint64_t reason)
{
	/*
	 * A rejected peer is not hard: the stressor hands the module plenty
	 * of deliberately bad input - dead sockets, colliding addresses -
	 * and being refused for that is expected rather than a sign that
	 * nothing will ever work here.
	 */
	return (reason == OVPN_FAIL_NETNS) ||
	       (reason == OVPN_FAIL_IFACE);
}

/*
 *  ovpn_tun_nlerror()
 *	the rtnetlink and genl helpers below return a negative errno, either
 *	lifted from the kernel's NLMSG_ERROR payload or set locally, and they
 *	never touch errno; describe the return code rather than reporting a
 *	stale errno. Note that -1 is a meaningful value here (-EPERM), which
 *	is what an unprivileged caller gets back from the kernel.
 */
static const char *ovpn_tun_nlerror(const int ret)
{
	return (ret < 0) ? strerror(-ret) : "no error";
}

/*  create a veth pair (name <-> peer) in the current netns */
static int ovpn_tun_veth_create(ovpn_ctx_t *o, const char *name, const char *peer)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[512];
	} req;
	struct rtattr *linkinfo, *infodata, *peerinfo;

	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_EXCL;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;

	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_IFNAME, name, strlen(name) + 1) < 0)
		return -EMSGSIZE;
	linkinfo = ovpn_nest_start(o, &req.n, sizeof(req), IFLA_LINKINFO);
	if (!linkinfo)
		return -EMSGSIZE;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_INFO_KIND, "veth", 5) < 0)
		return -EMSGSIZE;
	infodata = ovpn_nest_start(o, &req.n, sizeof(req), IFLA_INFO_DATA);
	if (!infodata)
		return -EMSGSIZE;
	peerinfo = ovpn_nest_start(o, &req.n, sizeof(req), VETH_INFO_PEER);
	if (!peerinfo)
		return -EMSGSIZE;
	req.n.nlmsg_len += NLMSG_ALIGN(sizeof(struct ifinfomsg));	/* peer ifinfomsg */
	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_IFNAME, peer, strlen(peer) + 1) < 0)
		return -EMSGSIZE;
	ovpn_nest_end(&req.n, peerinfo);
	ovpn_nest_end(&req.n, infodata);
	ovpn_nest_end(&req.n, linkinfo);

	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*  delete link 'name' by name (removes the whole veth pair) */
static int ovpn_tun_link_del(ovpn_ctx_t *o, const char *name)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[64];
	} req;

	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_DELLINK;
	req.i.ifi_family = AF_UNSPEC;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_IFNAME, name, strlen(name) + 1) < 0)
		return -EMSGSIZE;
	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*  set the MTU of link 'name' (in the current netns) */
static int ovpn_tun_link_mtu(ovpn_ctx_t *o, const char *name, const uint32_t mtu)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[64];
	} req;
	const unsigned int idx = if_nametoindex(name);

	if (idx == 0)
		return -ENODEV;
	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;
	req.i.ifi_index = (int)idx;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_MTU, &mtu, sizeof(mtu)) < 0)
		return -EMSGSIZE;
	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*  move link 'name' into the network namespace owned by pid */
static int ovpn_tun_link_move(ovpn_ctx_t *o, const char *name, const pid_t pid)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[64];
	} req;
	uint32_t nspid = (uint32_t)pid;
	const unsigned int idx = if_nametoindex(name);

	if (idx == 0)
		return -ENODEV;
	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;
	req.i.ifi_index = (int)idx;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFLA_NET_NS_PID, &nspid, sizeof(nspid)) < 0)
		return -EMSGSIZE;
	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*  bring link 'name' administratively up (in the current netns) */
static int ovpn_tun_link_up(ovpn_ctx_t *o, const char *name)
{
	struct {
		struct nlmsghdr n;
		struct ifinfomsg i;
		char buf[32];
	} req;
	const unsigned int idx = if_nametoindex(name);

	if (idx == 0)
		return -ENODEV;
	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.i));
	req.n.nlmsg_flags = NLM_F_REQUEST;
	req.n.nlmsg_type = RTM_NEWLINK;
	req.i.ifi_family = AF_UNSPEC;
	req.i.ifi_index = (int)idx;
	req.i.ifi_flags = IFF_UP;
	req.i.ifi_change = IFF_UP;
	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*  assign an IPv4 /plen address to link 'name' (in the current netns) */
static int ovpn_tun_addr_add(
	ovpn_ctx_t *o,
	const char *name,
	const uint8_t ip[4],
	const uint8_t plen)
{
	struct {
		struct nlmsghdr n;
		struct ifaddrmsg a;
		char buf[64];
	} req;
	const unsigned int idx = if_nametoindex(name);

	if (idx == 0)
		return -ENODEV;
	(void)shim_memset(&req, 0, sizeof(req));
	req.n.nlmsg_len = NLMSG_LENGTH(sizeof(req.a));
	req.n.nlmsg_flags = NLM_F_REQUEST | NLM_F_CREATE | NLM_F_REPLACE;
	req.n.nlmsg_type = RTM_NEWADDR;
	req.a.ifa_family = AF_INET;
	req.a.ifa_prefixlen = plen;
	req.a.ifa_index = idx;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFA_LOCAL, ip, 4) < 0)
		return -EMSGSIZE;
	if (ovpn_addattr(o, &req.n, sizeof(req), IFA_ADDRESS, ip, 4) < 0)
		return -EMSGSIZE;
	return ovpn_rt_send(o, &req.n, 0, 0, NULL, NULL);
}

/*
 *  ovpn_tunnel_add_peer()
 *	add a peer (+ a key) to the current ovpn interface. vpn==NULL for a
 *	P2P peer (no per-peer VPN IP); non-NULL for an MP peer. is_server
 *	selects the key direction. Returns 0 on success, or the negative
 *	netlink error from the failing peer/key command.
 */
static int ovpn_tunnel_add_peer(
	ovpn_ctx_t *o,
	const uint32_t peer_id,
	const uint8_t remote[4],
	const uint16_t rport,
	const uint8_t vpn[4],
	const bool is_server,
	const bool is_tcp)
{
	int ret;

	o->peer_id = peer_id;
	(void)shim_memset(&o->remote, 0, sizeof(o->remote));
	o->remote.in4.sin_family = AF_INET;
	o->remote.in4.sin_port = htons(rport);
	(void)shim_memcpy(&o->remote.in4.sin_addr, remote, 4);
	if (vpn) {
		(void)shim_memset(&o->peer_ip, 0, sizeof(o->peer_ip));
		o->peer_ip.in4.sin_family = AF_INET;
		(void)shim_memcpy(&o->peer_ip.in4.sin_addr, vpn, 4);
		o->peer_ip_set = true;
	} else {
		o->peer_ip_set = false;
	}
	/* for TCP the remote is implicit in the connected socket */
	ret = ovpn_new_peer(o, is_tcp);
	if (ret < 0)
		return ret;

	/*
	 * install the same fixed key in BOTH slots so a later primary<->
	 * secondary swap keeps a valid key active (non-destructive swap
	 * under traffic). Same bytes on client and server so decrypt works.
	 */
	o->cipher = OVPN_CIPHER_ALG_AES_GCM;
	o->key_dir = is_server ? SHIM_KEY_DIR_IN : SHIM_KEY_DIR_OUT;
	(void)shim_memset(o->key_enc, OVPN_TUN_KEY_BYTE, KEY_LEN);
	(void)shim_memset(o->key_dec, OVPN_TUN_KEY_BYTE, KEY_LEN);
	(void)shim_memset(o->nonce, OVPN_TUN_KEY_BYTE, NONCE_LEN);
	o->key_slot = OVPN_KEY_SLOT_PRIMARY;
	o->key_id = 0;
	ret = ovpn_new_key(o);
	if (ret < 0)
		return ret;
	o->key_slot = OVPN_KEY_SLOT_SECONDARY;
	o->key_id = 1;
	return ovpn_new_key(o);
}

/*
 *  ovpn_tunnel_inject()
 *	fire a burst of UDP datagrams (random size and fill, fixed port so
 *	the server can bind it) at the peer inner IP; routes via tun0 so the
 *	kernel encrypts them. Non-blocking and not drained, leaving frames in
 *	flight at teardown.
 */
static void ovpn_tunnel_inject(const uint8_t dst_in[4], const uint32_t packets)
{
	/*
	 * 64K rather than one MTU: the interesting sizes are the ones that
	 * straddle a boundary. A datagram that exactly fills the path, one
	 * byte over it, and one large enough to fragment several times all
	 * take different routes through the encapsulation.
	 */
	static uint8_t payload[65536];
	int s;
	struct sockaddr_in dst;
	uint32_t i;
	size_t len;

	switch (stress_mwc8() % 8) {
	case 0:
		len = 1;			/* smallest datagram there is */
		break;
	case 1:
		len = 1472;			/* exactly fills a 1500 byte path */
		break;
	case 2:
		len = 1473;			/* one over, so it fragments */
		break;
	case 3:
		len = 8192;
		break;
	case 4:
		len = 65507;			/* largest a UDP datagram can be */
		break;
	default:
		len = 1 + (stress_mwc16() % 1500);
		break;
	}

	if (packets == 0)
		return;
	s = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
	if (s < 0)
		return;
	(void)shim_memset(&dst, 0, sizeof(dst));
	dst.sin_family = AF_INET;
	dst.sin_port = htons(OVPN_TUN_DATA_PORT);	/* fixed so the server can receive */
	(void)shim_memcpy(&dst.sin_addr, dst_in, 4);
	(void)shim_memset(payload, (int)stress_mwc8(), len);
	for (i = 0; i < packets; i++) {
		const ssize_t n = sendto(s, payload, len, MSG_DONTWAIT,
			   (struct sockaddr *)&dst, sizeof(dst));

		if (n < 0)
			break;
	}
	(void)close(s);
}

/*
 *  ovpn_tunnel_drain()
 *	drain the datagrams that arrived (decrypted) on the server's inner UDP
 *	socket. The bytes are not counted here - the kernel's per-peer counters
 *	are used for that - but the socket still has to be emptied or its
 *	receive buffer fills up and the kernel starts dropping decrypted
 *	packets, which would throttle the very path being stressed.
 */
static void ovpn_tunnel_drain(const int fd)
{
	uint8_t buf[2048];

	if (fd < 0)
		return;
	for (;;) {
		const ssize_t n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT);

		if (n <= 0)
			break;
	}
}

/*
 *  ovpn_tunnel_churn()
 *	run a random sequence of live DCO operations for entropy.
 *
 *	On an ordinary cycle the peer that carries traffic (id 1) only gets
 *	non-destructive operations - a primary<->secondary key swap under
 *	load, get/set - so encrypt and decrypt stay coherent and the crypto
 *	hot path keeps running, while the destructive key and peer lifecycle
 *	churn is confined to phantom peers that carry nothing.
 *
 *	On a destructive cycle that protection is lifted and the live peer
 *	gets the lot: its in-use key slot deleted, a rekey to material the
 *	other end does not have, the peer itself deleted, all while traffic
 *	is still being injected. Keeping an object alive whenever anything
 *	might be using it is what stops a stressor from finding lifecycle
 *	bugs, so a share of cycles has to be willing to lose the tunnel.
 *	Such a cycle reports no traffic, which is the intended trade.
 */
static void ovpn_tunnel_churn(
	ovpn_ctx_t *o,
	const bool is_server,
	const bool is_tcp,
	const bool destructive)
{
	const int ops = 1 + (int)(stress_mwc8() % 8);
	int i;

	for (i = 0; i < ops; i++) {
		/*
		 * server (MP). Cases 0 to 2 act on the real peer and their
		 * failures are worth seeing; 3 to 6 act on phantom peers with
		 * random ids that mostly do not exist, so those are expected
		 * to fail and must not be logged - a minute of churn otherwise
		 * buries the log in tens of thousands of expected ENOENTs.
		 * Unused on the client path below, which has its own selection.
		 */
		const int op = (int)(stress_mwc8() % (destructive ? 14 : 7));

		o->expect_failure = false;
		if (!is_server) {
			/* client (P2P): only ever peer 1, the live one */
			o->peer_id = 1;
			switch (stress_mwc8() % (destructive ? 7 : 4)) {
			case 0:		/* swap slots under active traffic */
				(void)ovpn_swap_keys(o);
				break;
			case 1:
				o->keepalive_interval = stress_mwc8();
				o->keepalive_timeout = stress_mwc8();
				(void)ovpn_set_peer(o);
				break;
			case 2:
				(void)ovpn_get_peer(o);
				break;
			case 3:
				o->key_slot = OVPN_KEY_SLOT_PRIMARY;
				(void)ovpn_get_key(o);
				break;
			case 4:		/* drop the key the traffic is using */
				o->key_slot = stress_mwc1() ?
					OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
				(void)ovpn_del_key(o);
				break;
			case 5:		/* rekey to material the server does not have */
				o->key_slot = stress_mwc1() ?
					OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
				o->key_id = stress_mwc8() & 7;
				o->cipher = ovpn_tun_cipher();
				if (ovpn_generate_key(o) == 0)
					(void)ovpn_new_key(o);
				break;
			case 6:		/* delete the peer from under the traffic */
				(void)ovpn_del_peer(o);
				break;
			}
			continue;
		}

		/* only the phantom cases, 3 to 6, are expected to fail */
		o->expect_failure = (op >= 3) && (op <= 6);
		switch (op) {
		case 0:		/* non-destructive swap on the real peer under load */
			o->peer_id = 1;
			(void)ovpn_swap_keys(o);
			break;
		case 1:		/* query the real peer */
			o->peer_id = 1;
			(void)ovpn_get_peer(o);
			break;
		case 2:		/* keepalive on the real peer */
			o->peer_id = 1;
			o->keepalive_interval = stress_mwc8();
			o->keepalive_timeout = stress_mwc8();
			(void)ovpn_set_peer(o);
			break;
		case 3: {	/* add a phantom peer (bounded id range) */
			const uint8_t rem[4] = { 172, 16, stress_mwc8(), stress_mwc8() };
			uint8_t vpn[4];

			ovpn_tun_phantom_vpn_ip(vpn);

			/* phantom peers need no real connection; only meaningful
			 * for UDP (a TCP peer is 1:1 with an accepted socket) */
			if (!is_tcp)
				(void)ovpn_tunnel_add_peer(o, ovpn_tun_phantom_id(),
					rem, (uint16_t)(1024 + (stress_mwc16() % 60000)), vpn, true, false);
			break;
		}
		case 4:		/* delete a phantom peer (varied id/reason) */
			o->peer_id = ovpn_tun_phantom_id();
			(void)ovpn_del_peer(o);
			break;
		case 5:		/* destructive rekey on a phantom peer */
			o->peer_id = ovpn_tun_phantom_id();
			o->key_slot = stress_mwc1() ? OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
			o->key_id = stress_mwc8() & 7;
			o->cipher = ovpn_tun_cipher();
			o->key_dir = SHIM_KEY_DIR_IN;
			(void)shim_memset(o->key_enc, OVPN_TUN_KEY_BYTE, KEY_LEN);
			(void)shim_memset(o->key_dec, OVPN_TUN_KEY_BYTE, KEY_LEN);
			(void)shim_memset(o->nonce, OVPN_TUN_KEY_BYTE, NONCE_LEN);
			(void)ovpn_new_key(o);
			break;
		case 6:		/* delete a phantom key slot */
			o->peer_id = ovpn_tun_phantom_id();
			o->key_slot = stress_mwc1() ? OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
			(void)ovpn_del_key(o);
			break;
		case 7:		/* destructive cycles only, on the live peer */
			o->peer_id = 1;
			o->key_slot = stress_mwc1() ?
				OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
			(void)ovpn_del_key(o);
			break;
		case 8:
			o->peer_id = 1;
			o->key_slot = stress_mwc1() ?
				OVPN_KEY_SLOT_PRIMARY : OVPN_KEY_SLOT_SECONDARY;
			o->key_id = stress_mwc8() & 7;
			o->cipher = ovpn_tun_cipher();
			if (ovpn_generate_key(o) == 0)
				(void)ovpn_new_key(o);
			break;
		case 9:
			o->peer_id = 1;
			(void)ovpn_del_peer(o);
			break;
		case 10:	/* tear the interface down with peers attached */
			(void)ovpn_tun_link_del(o, o->ifname);
			break;
		case 11:	/* .. and put it straight back, reusing the name */
			(void)ovpn_tun_link_del(o, o->ifname);
			if (ovpn_new_iface(o) == 0)
				o->ifindex = if_nametoindex(o->ifname);
			break;
		case 12:	/* half close the transport under the module */
			if (o->socket >= 0)
				(void)shutdown(o->socket, SHUT_RDWR);
			break;
		case 13:	/* drop our reference while peers still hold theirs */
			if (o->socket >= 0) {
				(void)close(o->socket);
				o->socket = -1;
			}
			break;
		}
	}
	o->expect_failure = false;
}

/*
 *  ovpn_tunnel_tcp_listen()
 *	TCP transport server side, first half: bind the underlay port and
 *	start listening. Called before the netlink setup so that the client's
 *	connect() always has somewhere to land, whatever order the two
 *	children happen to be scheduled in. Returns the listening fd or -1.
 */
static int ovpn_tunnel_tcp_listen(void)
{
	int lfd;
	struct sockaddr_in a;
	const int opt = 1;

	/* non-blocking so accept() after select() can never block */
	lfd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
	if (lfd < 0)
		return -1;
	(void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
	(void)shim_memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(OVPN_TUN_PORT);
	a.sin_addr.s_addr = htonl(INADDR_ANY);
	if ((bind(lfd, (struct sockaddr *)&a, sizeof(a)) < 0) ||
	    (listen(lfd, SOMAXCONN) < 0)) {
		(void)close(lfd);
		return -1;
	}
	return lfd;
}

/*
 *  ovpn_tunnel_tcp_accept()
 *	TCP transport server side, second half: collect the connection the
 *	client made while the interface was being set up. Returns the
 *	accepted fd or -1.
 */
static int ovpn_tunnel_tcp_accept(const int lfd)
{
	struct timeval tv = {			/* bounded accept wait */
		.tv_sec = OVPN_TUN_ACCEPT_USEC / 1000000,
		.tv_usec = OVPN_TUN_ACCEPT_USEC % 1000000
	};
	fd_set rfds;

	FD_ZERO(&rfds);
	FD_SET(lfd, &rfds);
	if (select(lfd + 1, &rfds, NULL, NULL, &tv) <= 0)
		return -1;
	/*
	 * Whatever comes out of the accept queue is handed to the module as
	 * it is, including a connection whose client gave up waiting and has
	 * already closed or reset it. Feeding a dead socket to
	 * OVPN_CMD_PEER_NEW is a legitimate thing to test, and the module
	 * refusing it with EINVAL is the correct answer rather than a
	 * problem, so the socket is deliberately not vetted here.
	 */
	return accept(lfd, NULL, NULL);
}

/*
 *  ovpn_tunnel_tcp_client()
 *	TCP transport client side: connect to the server underlay, retrying
 *	while the server is not yet listening. Returns the connected fd or -1.
 */
static int ovpn_tunnel_tcp_client(const uint8_t server[4])
{
	struct sockaddr_in a;
	int i;

	(void)shim_memset(&a, 0, sizeof(a));
	a.sin_family = AF_INET;
	a.sin_port = htons(OVPN_TUN_PORT);
	(void)shim_memcpy(&a.sin_addr, server, 4);

	/*
	 * non-blocking connect with a bounded per-attempt wait: a blocking
	 * connect() could stall for the whole SYN timeout if the server is
	 * not listening yet and the SYN is black-holed.
	 */
	for (i = 0; i < OVPN_TUN_CONNECT_TRIES; i++) {
		const int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
		bool waited = false;
		int ret;

		if (fd < 0)
			return -1;
		ret = connect(fd, (struct sockaddr *)&a, sizeof(a));
		if (ret == 0)
			return fd;			/* immediate connect */
		if (errno == EINPROGRESS) {
			struct timeval tv = {
				.tv_sec = OVPN_TUN_CONNECT_USEC / 1000000,
				.tv_usec = OVPN_TUN_CONNECT_USEC % 1000000
			};
			fd_set wfds;
			int err = 0;
			socklen_t elen = sizeof(err);

			FD_ZERO(&wfds);
			FD_SET(fd, &wfds);
			ret = select(fd + 1, NULL, &wfds, NULL, &tv);
			if (ret == 0) {
				waited = true;		/* select burnt the budget */
			} else if ((ret > 0) &&
				   (getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &elen) == 0) &&
				   (err == 0)) {
				return fd;		/* connected */
			}
		}
		(void)close(fd);
		/*
		 * back off unless select() already consumed the per-attempt
		 * budget: a refused connect() on a non-blocking socket
		 * completes in microseconds (the peer RSTs and select()
		 * reports the fd writable straight away), so without this
		 * the whole retry budget is spent before the server has had
		 * a chance to reach listen().
		 */
		if (!waited)
			(void)shim_usleep(OVPN_TUN_CONNECT_USEC);
	}
	return -1;
}

/*
 *  ovpn_tunnel_endpoint()
 *	configure one end of the tunnel inside the caller's (already
 *	unshared) network namespace, reusing the existing genl helpers
 */
static int ovpn_tunnel_endpoint(
	ovpn_ctx_t *o,
	const bool is_server,
	const char *veth,
	const bool is_tcp)
{
	const uint8_t *ul_self = is_server ? ovpn_tun_ul_srv : ovpn_tun_ul_cli;
	const uint8_t *in_self = is_server ? ovpn_tun_in_srv : ovpn_tun_in_cli;
	const char *role = is_server ? "server" : "client";
	int ret;

	/* underlay: loopback + veth up, assign underlay address */
	(void)ovpn_tun_link_up(o, "lo");
	ret = ovpn_tun_link_up(o, veth);
	if (ret < 0) {
		pr_dbg("%s: tunnel(%s): veth '%s' up failed (migrated in?), ret=%d (%s)\n",
			o->args_name, role, veth, ret, ovpn_tun_nlerror(ret));
		ovpn_fail_set(OVPN_FAIL_ADDR);
		return -1;
	}
	ret = ovpn_tun_addr_add(o, veth, ul_self, 24);
	if (ret < 0) {
		pr_dbg("%s: tunnel(%s): underlay addr on '%s' failed, ret=%d (%s)\n",
			o->args_name, role, veth, ret, ovpn_tun_nlerror(ret));
		ovpn_fail_set(OVPN_FAIL_ADDR);
		return -1;
	}

	/*
	 * On a TCP cycle the server starts listening here, as soon as the
	 * underlay is addressed and before the interface work below. The two
	 * children run their setup concurrently with no ordering between
	 * them, so a client that reached connect() first would find nothing
	 * to connect to and the handshake would fail purely on scheduling.
	 *
	 * Listening has to come after the address, not merely first: the
	 * socket binds INADDR_ANY, and a SYN for the underlay address is not
	 * delivered to it until that address is actually local. Doing it here
	 * costs the client two netlink round trips of head start against the
	 * six or so it needs itself, which is a comfortable margin. The fd is
	 * parked in o->socket so the existing teardown closes it on any error
	 * path below.
	 */
	if (is_tcp && is_server) {
		o->socket = ovpn_tunnel_tcp_listen();
		if (o->socket < 0) {
			pr_dbg("%s: tunnel(server): cannot listen on the underlay "
				"port, errno=%d (%s)\n",
				o->args_name, errno, strerror(errno));
			ovpn_fail_set(OVPN_FAIL_TCP);
			return -1;
		}
	}

	/* create the ovpn interface: client is P2P (single peer), server MP */
	(void)shim_strscpy(o->ifname, "tun0", IFNAMSIZ);
	o->mode = is_server ? SHIM_OVPN_MODE_MP : SHIM_OVPN_MODE_P2P;
	o->mode_set = true;
	ret = ovpn_new_iface(o);
	if (ret < 0) {
		pr_dbg("%s: tunnel(%s): ovpn_new_iface failed (module loaded?), ret=%d (%s)\n",
			o->args_name, role, ret, ovpn_tun_nlerror(ret));
		ovpn_fail_set(OVPN_FAIL_IFACE);
		return -1;
	}
	/*
	 * record the new interface index: OVPN_A_IFINDEX is only added to
	 * genl peer/key messages when ifindex > 0, and without it the module
	 * rejects OVPN_CMD_PEER_NEW with EINVAL
	 */
	o->ifindex = if_nametoindex("tun0");
	if (o->ifindex == 0) {
		pr_dbg("%s: tunnel(%s): tun0 index lookup failed after create\n",
			o->args_name, role);
		ovpn_fail_set(OVPN_FAIL_IFACE);
		return -1;
	}
	ret = ovpn_tun_link_up(o, "tun0");
	if (ret < 0) {
		pr_dbg("%s: tunnel(%s): tun0 up failed, ret=%d (%s)\n",
			o->args_name, role, ret, ovpn_tun_nlerror(ret));
		ovpn_fail_set(OVPN_FAIL_ADDR);
		return -1;
	}
	ret = ovpn_tun_addr_add(o, "tun0", in_self, 24);	/* inner IP */
	if (ret < 0) {
		pr_dbg("%s: tunnel(%s): inner addr on tun0 failed, ret=%d (%s)\n",
			o->args_name, role, ret, ovpn_tun_nlerror(ret));
		ovpn_fail_set(OVPN_FAIL_ADDR);
		return -1;
	}

	/*
	 * Sometimes move the tunnel MTU off the default, so that the size at
	 * which the stack decides to fragment is not always the same one, and
	 * occasionally is awkward. Failure is not fatal: the cycle simply
	 * runs at whatever MTU the interface came up with.
	 */
	if ((stress_mwc8() % 4) == 0) {
		static const uint32_t mtus[] = { 576, 1000, 1281, 1500, 9000 };

		(void)ovpn_tun_link_mtu(o, "tun0",
			mtus[stress_mwc8() % SIZEOF_ARRAY(mtus)]);
	}

	/*
	 * transport socket. UDP: bind the underlay port (connectionless).
	 * TCP: the server listens and accepts, the client connects, over the
	 * veth, and the connected socket is handed to the DCO peer.
	 */
	o->lport = OVPN_TUN_PORT;
	o->sa_family = AF_INET;
	if (is_tcp) {
		if (is_server) {
			const int lfd = o->socket;

			o->socket = ovpn_tunnel_tcp_accept(lfd);
			(void)close(lfd);
		} else {
			o->socket = ovpn_tunnel_tcp_client(ovpn_tun_ul_srv);
		}
		if (o->socket < 0) {
			pr_dbg("%s: tunnel(%s): TCP transport handshake failed\n",
				o->args_name, role);
			ovpn_fail_set(OVPN_FAIL_TCP);
			return -1;
		}
	} else if (ovpn_udp_socket(o, AF_INET) < 0) {
		pr_dbg("%s: tunnel(%s): transport socket failed, errno=%d (%s)\n",
			o->args_name, role, errno, strerror(errno));
		ovpn_fail_set(OVPN_FAIL_RESOURCE);
		return -1;
	}

	/*
	 * peers. The client is P2P: one peer (id 1) to the server, no VPN IP.
	 * The server is MP: the real peer for the client (id 1, so the peer_id
	 * carried in data packets matches the client's) plus a random
	 * population of phantom peers with random ids / VPN IPs / remotes to
	 * exercise the MP peer table. The fixed shared key keeps the real
	 * peer's encrypt/decrypt coherent.
	 */
	if (is_server) {
		uint32_t n, i;

		ret = ovpn_tunnel_add_peer(o, 1, ovpn_tun_ul_cli, OVPN_TUN_PORT,
					   ovpn_tun_in_cli, true, is_tcp);
		if (ret < 0) {
			pr_dbg("%s: tunnel(server): add real peer failed, ret=%d (%s)\n",
				o->args_name, ret, ovpn_tun_nlerror(ret));
			ovpn_fail_set(OVPN_FAIL_PEER);
			return -1;
		}
		/*
		 * phantom peers only for UDP (a TCP peer needs its own
		 * accepted socket). Failures here are expected and ignored:
		 * the random ids/addresses are deliberate fuzzing, only the
		 * real peer above has to succeed.
		 */
		n = is_tcp ? 0 : (stress_mwc8() % 16);
		o->expect_failure = true;	/* random ids, failures expected */
		for (i = 0; i < n; i++) {
			const uint8_t rem[4] = { 172, 16, stress_mwc8(), stress_mwc8() };
			uint8_t vpn[4];

			ovpn_tun_phantom_vpn_ip(vpn);

			(void)ovpn_tunnel_add_peer(o, 2 + i, rem,
				(uint16_t)(1024 + (stress_mwc16() % 60000)), vpn, true, false);
		}
		o->expect_failure = false;
	} else {
		ret = ovpn_tunnel_add_peer(o, 1, ovpn_tun_ul_srv, OVPN_TUN_PORT,
					  NULL, false, is_tcp);
		if (ret < 0) {
			pr_dbg("%s: tunnel(client): add peer failed, ret=%d (%s)\n",
				o->args_name, ret, ovpn_tun_nlerror(ret));
			ovpn_fail_set(OVPN_FAIL_PEER);
			return -1;
		}
	}

	return 0;
}

/*
 *  ovpn_tunnel_child()
 *	runs in a fresh network namespace and configures one endpoint.
 *	Exits EXIT_SUCCESS only if the endpoint came up and was stressed,
 *	EXIT_NO_RESOURCE otherwise, so the parent can tell a working cycle
 *	apart from "the kernel will not let us build a tunnel at all".
 */
static void NORETURN ovpn_tunnel_child(
	stress_args_t *args,
	const bool is_server,
	const char *veth,
	const int rdyfd,
	const int gofd,
	const uint32_t packets,
	const bool is_tcp,
	const bool destructive,
	const int parent_cpu)
{
	ovpn_ctx_t ovpn;
	const char *role = is_server ? "server" : "client";
	char c = OVPN_TUN_NO_NETNS;
	int rc = EXIT_NO_RESOURCE;

	/*
	 * PDEATHSIG so a child cannot outlive the instance, and a reseed
	 * because both children inherit the parent's mwc state and would
	 * otherwise generate identical "random" tunnel parameters
	 */
	stress_parent_died_alarm();
	stress_make_it_fail_set();
	(void)stress_affinity_change_cpu(args, parent_cpu);
	(void)stress_sched_settings_apply(true);
	stress_mwc_reseed();

	if (shim_unshare(CLONE_NEWNET) == 0) {
		c = OVPN_TUN_READY;
	} else {
		ovpn_fail_set(OVPN_FAIL_NETNS);
		pr_dbg("%s: tunnel(%s): unshare(CLONE_NEWNET) failed, errno=%d (%s)\n",
			args->name, role, errno, strerror(errno));
	}
	if (write(rdyfd, &c, 1) != 1)
		_exit(EXIT_NO_RESOURCE);
	if (c != OVPN_TUN_READY)
		_exit(EXIT_NO_RESOURCE);		/* needs CAP_SYS_ADMIN */
	if (read(gofd, &c, 1) != 1)			/* wait for veth migration */
		_exit(EXIT_NO_RESOURCE);
	if (c != OVPN_TUN_GO)
		_exit(EXIT_NO_RESOURCE);

	(void)shim_memset(&ovpn, 0, sizeof(ovpn));
	ovpn.args_name = args->name;
	ovpn.sa_family = AF_INET;
	ovpn.cipher = OVPN_CIPHER_ALG_NONE;
	ovpn.socket = -1;

	if (ovpn_tunnel_endpoint(&ovpn, is_server, veth, is_tcp) == 0) {
		/*
		 * keep the freshly-built tunnel live for a random number of
		 * iterations, mixing data-path traffic with control-plane
		 * churn, then return so the parent tears it down and rebuilds.
		 * The random duration means some tunnels are short-lived
		 * (teardown-race heavy) and some long-lived (throughput heavy).
		 * The server churns its MP peer table (create/swap/delete); the
		 * client injects traffic and rekeys the live tunnel now and then.
		 */
		const uint8_t *in_peer = ovpn_tun_in_srv;
		/*
		 * Most cycles are deliberately short, so that build and
		 * teardown dominate: the lifecycle races live there, not in
		 * the steady state. The rest run long enough to get the crypto
		 * path warm and to accumulate meaningful traffic counters.
		 */
		const int iters = ((stress_mwc8() % 5) < 3) ?
			1 + (int)(stress_mwc8() % 8) :
			1 + (int)(stress_mwc16() % 256);
		/* this cycle's bytes land in the counter for its transport */
		uint64_t *const bytec = &ovpn_bytes[is_server ?
			(is_tcp ? OVPN_CTR_RX_TCP : OVPN_CTR_RX_UDP) :
			(is_tcp ? OVPN_CTR_TX_TCP : OVPN_CTR_TX_UDP)];
		pid_t churner = -1;
		int i, rxfd = -1;

		/* server: receive decrypted datagrams on the inner data port */
		if (is_server) {
			rxfd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, 0);
			if (rxfd >= 0) {
				struct sockaddr_in a;
				const int rcvbuf = 4 * 1024 * 1024;

				/* large recv buffer: the server drains only between
				 * churn ops, so bound the drops of decrypted packets */
				(void)setsockopt(rxfd, SOL_SOCKET, SO_RCVBUF,
						 &rcvbuf, sizeof(rcvbuf));
				(void)shim_memset(&a, 0, sizeof(a));
				a.sin_family = AF_INET;
				a.sin_port = htons(OVPN_TUN_DATA_PORT);
				a.sin_addr.s_addr = htonl(INADDR_ANY);
				if (bind(rxfd, (struct sockaddr *)&a, sizeof(a)) < 0) {
					(void)close(rxfd);
					rxfd = -1;
				}
			}
		}

		/*
		 * A second process in this namespace, working the same
		 * interface. Every command the stressor issues is otherwise
		 * serialised inside one process, so the peer table is never
		 * manipulated concurrently - and concurrent use of an object
		 * that is being destroyed is precisely what a lifecycle bug
		 * needs in order to show itself. The transport socket is
		 * inherited rather than reopened, so the churner can build
		 * peers on it; its copy of the fd closing on exit leaves the
		 * server's own reference untouched.
		 */
		if (is_server) {
			churner = fork();
			if (churner == 0) {
				ovpn_ctx_t peer_ctx = ovpn;

				/*
				 * PDEATHSIG is SIGALRM, and in a stress-ng
				 * process SIGALRM means "the whole run is
				 * stopping": the handler calls
				 * stress_bogo_max_ops_zero(), and since
				 * stress_continue() is a comparison against
				 * max_ops, zeroing it ends the loop of every
				 * instance of every stressor. So an endpoint
				 * that dies while its churner is alive would
				 * not merely lose its cycle, it would end the
				 * run - reported as a success, which is how a
				 * --timeout 300s run came to finish in 13s.
				 *
				 * The churner wants none of that meaning. It
				 * only has to die with its parent, which the
				 * default action does on its own, without
				 * running a handler. Restore that action
				 * before arming PDEATHSIG rather than after,
				 * so a parent that dies in between cannot
				 * deliver the signal while the handler that
				 * ends the run is still installed.
				 */
				if (stress_signal_handler(args->name,
						SIGALRM, SIG_DFL, NULL) < 0)
					pr_dbg("%s: tunnel: could not restore "
						"the default SIGALRM in the "
						"churner, an orphaned churner "
						"can stop the run\n",
						args->name);
				stress_parent_died_alarm();
				stress_mwc_reseed();
				while (stress_continue(args))
					ovpn_tunnel_churn(&peer_ctx, true, is_tcp,
						destructive);
				_exit(EXIT_SUCCESS);
			}
		}

		for (i = 0; (i < iters) && stress_continue(args); i++) {
			if (is_server) {
				ovpn_tunnel_churn(&ovpn, true, is_tcp, destructive);
				ovpn_tunnel_drain(rxfd);
			} else {
				ovpn_tunnel_inject(in_peer, packets);
				if ((stress_mwc8() & 0xf) == 0)
					ovpn_tunnel_churn(&ovpn, false, is_tcp, destructive);
				/*
				 * the instance folds this into the bogo-op counter
				 * once the cycle is reaped: incrementing it from a
				 * child of a stressor risks leaving the counter in a
				 * torn state if the child is killed mid-update
				 */
				(void)__atomic_add_fetch(&ovpn_bytes[OVPN_CTR_OPS],
					1, __ATOMIC_RELAXED);
			}
		}
		if (churner > 0)
			(void)stress_kill_pid_wait(churner, NULL);
		if (rxfd >= 0)
			(void)close(rxfd);

		/*
		 * Final read of the kernel's own counters for the traffic-carrying
		 * peer, taken while the tunnel is still up. Each side reports the
		 * direction it is authoritative for: the client encrypted what its
		 * peer counts as VPN_TX, the server decrypted and delivered what
		 * its peer counts as VPN_RX.
		 */
		ovpn.peer_id = 1;
		ovpn.peer_stats_valid = false;
		if ((ovpn_get_peer(&ovpn) >= 0) && ovpn.peer_stats_valid) {
			(void)__atomic_add_fetch(bytec, is_server ?
				ovpn.peer_vpn_rx_bytes : ovpn.peer_vpn_tx_bytes,
				__ATOMIC_RELAXED);
		} else {
			pr_dbg("%s: tunnel(%s): no per-peer byte counters from the "
				"kernel, traffic metrics will read zero\n",
				args->name, role);
		}
		rc = EXIT_SUCCESS;
	}

	if (ovpn.socket >= 0)
		(void)close(ovpn.socket);
	(void)close(rdyfd);
	(void)close(gofd);
	_exit(rc);
}

/*
 *  ovpn_tun_pipe_close()
 *	close whichever ends of a sync pipe are still open
 */
static void ovpn_tun_pipe_close(int fds[2])
{
	if (fds[0] >= 0) {
		(void)close(fds[0]);
		fds[0] = -1;
	}
	if (fds[1] >= 0) {
		(void)close(fds[1]);
		fds[1] = -1;
	}
}

/*
 *  ovpn_tunnel_cycle()
 *	one full build/inject/teardown of a two-endpoint tunnel. Returns
 *	EXIT_SUCCESS when both endpoints came up and were stressed, and
 *	EXIT_NO_RESOURCE when the tunnel could not be built at all.
 */
static int ovpn_tunnel_cycle(
	stress_args_t *args,
	ovpn_ctx_t *root,
	const uint32_t id,
	const uint32_t packets,
	const bool is_tcp,
	const bool destructive)
{
	char vs[IFNAMSIZ], vc[IFNAMSIZ];
	int srdy[2] = { -1, -1 }, sgo[2] = { -1, -1 };
	int crdy[2] = { -1, -1 }, cgo[2] = { -1, -1 };
	char cs = OVPN_TUN_NO_NETNS, cc = OVPN_TUN_NO_NETNS;
	pid_t ps = -1, pc = -1;
	uint64_t ops_before, ops_after;
	int rc = EXIT_SUCCESS;
	int parent_cpu;
	int ret;

	ops_before = __atomic_load_n(&ovpn_bytes[OVPN_CTR_OPS], __ATOMIC_RELAXED);

	(void)snprintf(vs, sizeof(vs), "ovts%" PRIx32, id);
	(void)snprintf(vc, sizeof(vc), "ovtc%" PRIx32, id);

	/*
	 * clear any stale ends left by an interrupted previous cycle. Both
	 * names have to be tried: deleting one end of a live pair takes the
	 * other with it, but a cycle that migrated one end and then failed
	 * leaves the two halves in different namespaces, and a surviving
	 * client end would make every later create fail with EEXIST.
	 */
	(void)ovpn_tun_link_del(root, vs);
	(void)ovpn_tun_link_del(root, vc);

	ret = ovpn_tun_veth_create(root, vs, vc);
	if (ret < 0) {
		ovpn_fail_set(OVPN_FAIL_RESOURCE);
		pr_dbg("%s: tunnel: veth create %s<->%s failed, ret=%d (%s)\n",
			args->name, vs, vc, ret, ovpn_tun_nlerror(ret));
		return EXIT_NO_RESOURCE;
	}

	if ((pipe(srdy) < 0) || (pipe(sgo) < 0) ||
	    (pipe(crdy) < 0) || (pipe(cgo) < 0)) {
		ovpn_fail_set(OVPN_FAIL_RESOURCE);
		pr_dbg("%s: tunnel: pipe failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	parent_cpu = (int)stress_cpu_get();

	ps = fork();
	if (ps < 0) {
		ovpn_fail_set(OVPN_FAIL_RESOURCE);
		if (!stress_redo_fork(args, errno))
			pr_dbg("%s: tunnel: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}
	if (ps == 0) {
		(void)close(srdy[0]); (void)close(sgo[1]);
		(void)close(crdy[0]); (void)close(crdy[1]);
		(void)close(cgo[0]); (void)close(cgo[1]);
		ovpn_tunnel_child(args, true, vs, srdy[1], sgo[0],
				  packets, is_tcp, destructive, parent_cpu);
	}
	pc = fork();
	if (pc < 0) {
		ovpn_fail_set(OVPN_FAIL_RESOURCE);
		if (!stress_redo_fork(args, errno))
			pr_dbg("%s: tunnel: fork failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}
	if (pc == 0) {
		(void)close(crdy[0]); (void)close(cgo[1]);
		(void)close(srdy[0]); (void)close(srdy[1]);
		(void)close(sgo[0]); (void)close(sgo[1]);
		ovpn_tunnel_child(args, false, vc, crdy[1], cgo[0],
				  packets, is_tcp, destructive, parent_cpu);
	}

	/* parent: keep the read/ready and write/go ends */
	(void)close(srdy[1]); srdy[1] = -1;
	(void)close(sgo[0]); sgo[0] = -1;
	(void)close(crdy[1]); crdy[1] = -1;
	(void)close(cgo[0]); cgo[0] = -1;

	/* wait for both children to unshare their namespaces */
	if ((read(srdy[0], &cs, 1) != 1) || (read(crdy[0], &cc, 1) != 1)) {
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}
	/*
	 * A child that could not unshare has already exited, so it must not
	 * be handed the go-ahead: writing to a pipe whose read end is gone
	 * raises SIGPIPE and would take the whole instance down with it.
	 */
	if ((cs != OVPN_TUN_READY) || (cc != OVPN_TUN_READY)) {
		rc = EXIT_NO_RESOURCE;
		goto tidy;
	}

	/* migrate each veth end into its child's namespace */
	ret = ovpn_tun_link_move(root, vs, ps);
	if (ret < 0)
		pr_dbg("%s: tunnel: moving %s into server netns failed, ret=%d (%s)\n",
			args->name, vs, ret, ovpn_tun_nlerror(ret));
	ret = ovpn_tun_link_move(root, vc, pc);
	if (ret < 0)
		pr_dbg("%s: tunnel: moving %s into client netns failed, ret=%d (%s)\n",
			args->name, vc, ret, ovpn_tun_nlerror(ret));

	/*
	 * release both children to configure + inject. Both writes are always
	 * attempted: skipping the second would strand the other child in its
	 * read() until the tidy path closes the pipe.
	 */
	cs = OVPN_TUN_GO;
	if (write(sgo[1], &cs, 1) != 1)
		rc = EXIT_NO_RESOURCE;
	if (write(cgo[1], &cs, 1) != 1)
		rc = EXIT_NO_RESOURCE;
tidy:
	ovpn_tun_pipe_close(srdy);
	ovpn_tun_pipe_close(sgo);
	ovpn_tun_pipe_close(crdy);
	ovpn_tun_pipe_close(cgo);

	/*
	 * The children keep the tunnel live for a bounded, timeout-checked
	 * number of iterations and then exit on their own; closing the go
	 * pipes above releases any that are still waiting.
	 */
	if (ps > 0) {
		if (stress_wait_until_reaped(args, ps, SIGKILL, false) != EXIT_SUCCESS)
			rc = EXIT_NO_RESOURCE;
	}
	if (pc > 0) {
		if (stress_wait_until_reaped(args, pc, SIGKILL, false) != EXIT_SUCCESS)
			rc = EXIT_NO_RESOURCE;
	}

	/*
	 * Both children are gone, so fold the work they did into the bogo-op
	 * counter from here. Doing the accounting in the instance rather than
	 * in the children keeps the counter out of reach of a child that gets
	 * killed part way through an update, which is what the note on
	 * stress_bogo_inc() warns about. The cost is that --ovpn-ops can
	 * overshoot by at most one cycle's worth of bursts.
	 */
	ops_after = __atomic_load_n(&ovpn_bytes[OVPN_CTR_OPS], __ATOMIC_RELAXED);
	if (ops_after > ops_before)
		stress_bogo_add(args, ops_after - ops_before);

	return rc;
}

/*
 *  stress_ovpn_tunnel()
 *	--ovpn-tunnel mode entry point
 */
static int stress_ovpn_tunnel(stress_args_t *args)
{
	ovpn_ctx_t root;
	const size_t ctr_sz = sizeof(uint64_t) * OVPN_CTR_MAX;
	double tx_udp, tx_tcp, rx_udp, rx_tcp, duration;
	uint64_t reason = OVPN_FAIL_NONE;
	int rc = EXIT_SUCCESS;
	int fails = 0;

	(void)shim_memset(&root, 0, sizeof(root));
	root.args_name = args->name;
	root.sa_family = AF_INET;
	root.socket = -1;

	/*
	 * The tunnel mode needs CAP_SYS_ADMIN on top of the CAP_NET_ADMIN
	 * that stress_ovpn_supported() already checks for, because it has to
	 * unshare network namespaces; the control-plane mode does not, so
	 * this cannot be hoisted into the .supported handler.
	 */
	if (!stress_capabilities_check(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s: --ovpn-tunnel needs CAP_SYS_ADMIN to unshare "
			"network namespaces, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	/*
	 * Take a private network namespace for the whole instance: the veth
	 * pairs and the child namespaces then belong to it and the kernel
	 * reclaims them when the instance exits, so a killed run cannot
	 * leave interfaces behind in the host network namespace.
	 */
	if (shim_unshare(CLONE_NEWNET) < 0) {
		pr_inf_skip("%s: cannot unshare network namespace, errno=%d (%s), "
			"skipping stressor\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	/*
	 * The children are released through a pipe write, and a child that
	 * bailed out early has already closed its read end, so SIGPIPE has
	 * to be harmless here or a failing cycle would kill the instance.
	 */
	if (stress_signal_handler(args->name, SIGPIPE, SIG_IGN, NULL) < 0)
		return EXIT_NO_RESOURCE;

	/* shared per-direction, per-transport byte counters */
	ovpn_bytes = (uint64_t *)stress_mmap_anon_shared(ctr_sz,
			PROT_READ | PROT_WRITE);
	if (ovpn_bytes == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap %zu bytes of shared metrics%s, "
			"skipping stressor\n",
			args->name, ctr_sz, stress_memory_free_get());
		return EXIT_NO_RESOURCE;
	}
	(void)shim_memset(ovpn_bytes, 0, ctr_sz);
	stress_memory_anon_name_set(ovpn_bytes, ctr_sz, "ovpn-tunnel-metrics");

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	duration = stress_time_now();

	/* build / run-live / teardown, repeatedly, until the run ends;
	 * pick the transport (UDP or TCP) at random each cycle */
	do {
		const bool is_tcp = stress_mwc1();
		/*
		 * A share of cycles is willing to lose its tunnel, so that
		 * the peer carrying traffic can be torn apart while it is in
		 * use. Those cycles report no traffic, so the metrics are the
		 * rate over the cycles that survived.
		 */
		const bool destructive = (stress_mwc8() % 3) == 0;

		if (ovpn_tunnel_cycle(args, &root, args->instance,
				      OVPN_TUN_PACKETS, is_tcp,
				      destructive) == EXIT_SUCCESS) {
			fails = 0;
			continue;
		}
		/*
		 * Cycles are allowed to fail - the teardown races they exercise
		 * make that expected - but there is no point carrying on if
		 * nothing has ever worked. A hard failure says so on the first
		 * attempt; a soft one is timing or contention, so give those
		 * several tries before concluding anything, and report what
		 * actually went wrong rather than guessing at a cause.
		 */
		reason = __atomic_load_n(&ovpn_bytes[OVPN_CTR_FAIL], __ATOMIC_RELAXED);
		fails++;
		if (stress_bogo_get(args) > 0)
			continue;	/* it has worked before, keep going */
		if (ovpn_fail_is_hard(reason)) {
			pr_inf_skip("%s: %s, skipping stressor\n",
				args->name, ovpn_fail_str(reason));
			rc = EXIT_NO_RESOURCE;
			break;
		}
		if (fails >= OVPN_TUN_MAX_FAILS) {
			pr_inf_skip("%s: no DCO tunnel could be built in %d attempts (%s), "
				"skipping stressor\n", args->name, fails,
				ovpn_fail_str(reason));
			rc = EXIT_NO_RESOURCE;
			break;
		}
	} while (stress_continue(args));

	duration = stress_time_now() - duration;
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	/*
	 * Report rates rather than byte totals. A byte total conflates how much
	 * work was done with how long the instance ran and how many instances
	 * competed for the CPU, so byte totals from runs with different -ovpn
	 * counts or timeouts cannot be compared; a per-second figure can.
	 *
	 * The per-instance rates are then summed rather than averaged, because
	 * a destructive cycle is meant to lose its tunnel and an instance whose
	 * cycles were mostly destructive legitimately reports zero. Under a
	 * harmonic or geometric mean a single such instance takes the whole
	 * figure to zero however well the others did - at 20 instances that
	 * reported "decrypted 0.00 MB/s" for a run whose own per-peer counters
	 * showed 81MB of plaintext delivered. A sum is the aggregate throughput
	 * and is unbothered by a zero.
	 */
	tx_udp = (duration > 0.0) ? (double)ovpn_bytes[OVPN_CTR_TX_UDP] / (double)MB / duration : 0.0;
	tx_tcp = (duration > 0.0) ? (double)ovpn_bytes[OVPN_CTR_TX_TCP] / (double)MB / duration : 0.0;
	rx_udp = (duration > 0.0) ? (double)ovpn_bytes[OVPN_CTR_RX_UDP] / (double)MB / duration : 0.0;
	rx_tcp = (duration > 0.0) ? (double)ovpn_bytes[OVPN_CTR_RX_TCP] / (double)MB / duration : 0.0;

	/*
	 * These come from the kernel's own per-peer plaintext byte counters,
	 * not from a userspace estimate. All four are always reported, even
	 * when zero: the metrics of every instance are matched up by
	 * description, so the set must not vary between instances.
	 */
	stress_metrics_set(args, "MB per sec encrypted, UDP transport", tx_udp,
		STRESS_METRIC_TOTAL);
	stress_metrics_set(args, "MB per sec encrypted, TCP transport", tx_tcp,
		STRESS_METRIC_TOTAL);
	stress_metrics_set(args, "MB per sec decrypted, UDP transport", rx_udp,
		STRESS_METRIC_TOTAL);
	stress_metrics_set(args, "MB per sec decrypted, TCP transport", rx_tcp,
		STRESS_METRIC_TOTAL);
	pr_dbg("%s: tunnel MB/s tx(udp/tcp)=%.2f/%.2f rx(udp/tcp)=%.2f/%.2f\n",
		args->name, tx_udp, tx_tcp, rx_udp, rx_tcp);

	(void)stress_munmap_anon_shared((void *)ovpn_bytes, ctr_sz);
	ovpn_bytes = NULL;

	return rc;
}

static int stress_ovpn(stress_args_t *args)
{
	ovpn_ctx_t ovpn;
	bool ovpn_tunnel = false;
	int last_cmd = -1;
	static const ovpn_cmd_t cmds[] = {
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
	const size_t count = SIZEOF_ARRAY(cmds);

	(void)shim_memset(&ovpn, 0, sizeof(ovpn));
	ovpn.args_name = args->name;
	ovpn.sa_family = AF_INET;
	ovpn.cipher = OVPN_CIPHER_ALG_NONE;
	ovpn.peers_file = NULL;
	ovpn.socket = -1;

	(void)stress_setting_get("ovpn-tunnel", &ovpn_tunnel);
	if (ovpn_tunnel)
		return stress_ovpn_tunnel(args);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		int cmd;

		do {
			const size_t idx = stress_mwcsizemodn(count);

			cmd = cmds[idx];
		} while (cmd == last_cmd && count > 1);

		last_cmd = cmd;
		ovpn.cmd = (ovpn_cmd_t)cmd;

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

static const stress_exercises_t exercises[] = {
	STRESS_EX_SYSCALL("accept"),
	STRESS_EX_SYSCALL("bind"),
	STRESS_EX_SYSCALL("connect"),
	STRESS_EX_SYSCALL("fork"),
	STRESS_EX_SYSCALL("getsockname"),
	STRESS_EX_SYSCALL("getsockopt"),
	STRESS_EX_SYSCALL("listen"),
	STRESS_EX_SYSCALL("mmap"),
	STRESS_EX_SYSCALL("pipe"),
	STRESS_EX_SYSCALL("recv"),
	STRESS_EX_SYSCALL("recvmsg"),
	STRESS_EX_SYSCALL("select"),
	STRESS_EX_SYSCALL("sendmsg"),
	STRESS_EX_SYSCALL("sendto"),
	STRESS_EX_SYSCALL("setsockopt"),
	STRESS_EX_SYSCALL("shutdown"),
	STRESS_EX_SYSCALL("socket"),
	STRESS_EX_SYSCALL("unshare"),
	STRESS_EX_SYSCALL("waitpid"),

	STRESS_EX_LIBRARY("nl"),

	STRESS_EX_END,
};

const stressor_info_t stress_ovpn_info = {
	.stressor = stress_ovpn,
	.supported = stress_ovpn_supported,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help,
	.exercises = exercises,
};

#else

const stressor_info_t stress_ovpn_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_NONE,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without libnl3, without a linux/ovpn.h providing the ovpn netlink uapi, or built statically"
};

#endif /* HAVE_LIB_NL && HAVE_LINUX_OVPN_UAPI */
