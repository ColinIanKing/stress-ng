/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-net.h"

#include <sys/ioctl.h>

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#endif

#if defined(HAVE_LINUX_SOCKIOS_H)
#include <linux/sockios.h>
#endif

#include <netinet/in.h>

#define DCCP_BUF		(1024)	/* DCCP I/O buffer size */

#define DEFAULT_DCCP_PORT	(10000)

#define MIN_DCCP_MSGS		(1)
#define MAX_DCCP_MSGS		(10000000)
#define DEFAULT_DCCP_MSGS	(10000)

#define DCCP_OPT_SEND		(0x01)
#define DCCP_OPT_SENDMSG	(0x02)
#define DCCP_OPT_SENDMMSG	(0x03)


#define MSGVEC_SIZE		(4)

typedef struct {
	const char *optname;
	const int   opt;
} stress_dccp_opts_t;

static const stress_help_t help[] = {
	{ NULL,	"dccp N",		"start N workers exercising network DCCP I/O" },
	{ NULL,	"dccp-domain D",	"specify DCCP domain, default is ipv4" },
	{ NULL,	"dccp-if I",		"use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"dccp-ops N",		"stop after N DCCP  bogo operations" },
	{ NULL,	"dccp-opts option",	"DCCP data send options [send|sendmsg|sendmmsg]" },
	{ NULL,	"dccp-port P",		"use DCCP ports P to P + number of workers - 1" },
	{ NULL,	"dccp-msgs N",		"number of DCCP messages to send per connection" },
	{ NULL,	NULL,			NULL }
};

static const stress_dccp_opts_t dccp_options[] = {
	{ "send",	DCCP_OPT_SEND },
	{ "sendmsg",	DCCP_OPT_SENDMSG },
#if defined(HAVE_SENDMMSG)
	{ "sendmmsg",	DCCP_OPT_SENDMMSG },
#endif
};

static const char *stress_dccp_options(const size_t i)
{
	return (i < SIZEOF_ARRAY(dccp_options)) ? dccp_options[i].optname: NULL;
}

static int dccp_domain_mask = DOMAIN_INET | DOMAIN_INET6;

static const stress_opt_t opts[] = {
	{ OPT_dccp_domain, "dccp-domain", TYPE_ID_INT_DOMAIN,    0, 0, &dccp_domain_mask },
	{ OPT_dccp_if,     "dccp-if",     TYPE_ID_STR,           0, 0, NULL },
	{ OPT_dccp_msgs,   "dccp-msgs",   TYPE_ID_SIZE_T,        MIN_DCCP_MSGS, MAX_DCCP_MSGS, NULL },
	{ OPT_dccp_opts,   "dccp-opts",	  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_dccp_options },
	{ OPT_dccp_port,   "dccp-port",   TYPE_ID_INT_PORT,      MIN_PORT, MAX_PORT, NULL },
	END_OPT,
};

#if defined(SOCK_DCCP) &&	\
    defined(IPPROTO_DCCP)

/*
 *  stress_dccp_client()
 *	client reader
 */
static int stress_dccp_client(
	stress_args_t *args,
	const pid_t mypid,
	const int dccp_port,
	const int dccp_domain,
	const char *dccp_if)
{
	struct sockaddr *addr;

	stress_parent_died_alarm();

	do {
		char buf[DCCP_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
retry:
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_FAILURE;
		if ((fd = socket(dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
			if ((errno == ESOCKTNOSUPPORT) ||
			    (errno == EPROTONOSUPPORT)) {
				/*
				 *  Protocol not supported - then return
				 *  EXIT_NOT_IMPLEMENTED and skip the test
				 */
				return EXIT_NOT_IMPLEMENTED;
			}
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if (stress_set_sockaddr_if(args->name, args->instance, mypid,
				dccp_domain, dccp_port, dccp_if,
				&addr, &addr_len, NET_ADDR_ANY) < 0) {
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (connect(fd, addr, addr_len) < 0) {
			int err = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				errno = err;
				pr_fail("%s: connect failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				return EXIT_FAILURE;
			}
			goto retry;
		}

		do {
			const ssize_t n = recv(fd, buf, sizeof(buf), 0);

			if (n == 0)
				break;
			if (n < 0) {
				if (errno != EINTR)
					pr_dbg("%s: recv failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				break;
			}
		} while (stress_continue(args));
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (stress_continue(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (dccp_domain == AF_UNIX) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_dccp_server()
 *	server writer
 */
static int stress_dccp_server(
	stress_args_t *args,
	const int mypid,
	const int dccp_port,
	const int dccp_domain,
	const char *dccp_if,
	const int dccp_opts)
{
	char buf[DCCP_BUF];
	int fd, so_reuseaddr = 1, rc = EXIT_SUCCESS;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	uint64_t msgs = 0;
	double t1 = 0.0, t2 = 0.0, dt;
	size_t dccp_msgs = DEFAULT_DCCP_MSGS;

	if (!stress_get_setting("dccp-msgs", &dccp_msgs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			dccp_msgs = MAX_DCCP_MSGS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			dccp_msgs = MIN_DCCP_MSGS;
	}

	if (stress_sig_stop_stressing(args->name, SIGALRM) < 0) {
		rc = EXIT_FAILURE;
		goto die;
	}
	if ((fd = socket(dccp_domain, SOCK_DCCP, IPPROTO_DCCP)) < 0) {
		if ((errno == ESOCKTNOSUPPORT) ||
		    (errno == EPROTONOSUPPORT)) {
			/*
			 *  Protocol not supported - then return
			 *  EXIT_NOT_IMPLEMENTED and skip the test
			 */
			if (stress_instance_zero(args))
				pr_inf_skip("%s: DCCP protocol not supported, "
					"skipping stressor\n", args->name);
			return EXIT_NOT_IMPLEMENTED;
		}
		rc = stress_exit_status(errno);
		pr_fail("%s: socket failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die;
	}
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
		&so_reuseaddr, sizeof(so_reuseaddr)) < 0) {
		pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	if (stress_set_sockaddr_if(args->name, args->instance, mypid,
		dccp_domain, dccp_port, dccp_if,
		&addr, &addr_len, NET_ADDR_ANY) < 0) {
		goto die_close;
	}
	if (bind(fd, addr, addr_len) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: bind failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto die_close;
	}
	if (listen(fd, 10) < 0) {
		pr_fail("%s: listen failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		rc = EXIT_FAILURE;
		goto die_close;
	}

	t1 = stress_time_now();
	do {
		int sfd;

		if (UNLIKELY(!stress_continue(args)))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (sfd >= 0) {
			size_t i, j, k;
			struct sockaddr saddr;
			socklen_t len;
			int sndbuf;
			struct msghdr msg;
			struct iovec vec[sizeof(buf)/16];
#if defined(HAVE_SENDMMSG)
			struct mmsghdr msgvec[MSGVEC_SIZE];
#endif
			len = sizeof(saddr);
			if (getsockname(fd, &saddr, &len) < 0) {
				pr_dbg("%s: getsockname failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			}
			len = sizeof(sndbuf);
			if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sndbuf, &len) < 0) {
				pr_dbg("%s: getsockopt SO_SNDBUF failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				(void)close(sfd);
				break;
			}

			(void)shim_memset(buf, stress_ascii64[stress_bogo_get(args) & 63], sizeof(buf));
			k = 0;
			switch (dccp_opts) {
			case DCCP_OPT_SEND:
				do {
					for (i = 16; i < sizeof(buf); i += 16, k++) {
						ssize_t ret;
again:
						ret = send(sfd, buf, i, 0);
						if (ret < 0) {
							if (errno == EAGAIN)
								goto again;
							if (errno != EINTR)
								pr_dbg("%s: send failed, errno=%d (%s)\n",
									args->name, errno, strerror(errno));
							break;
						} else
							msgs++;
					}
					stress_bogo_inc(args);
				} while (LIKELY(stress_continue(args) && (k < dccp_msgs)));
				break;
			case DCCP_OPT_SENDMSG:
				do {
					for (j = 0, i = 16; i < sizeof(buf); i += 16, j++, k++) {
						vec[j].iov_base = buf;
						vec[j].iov_len = i;
					}
					(void)shim_memset(&msg, 0, sizeof(msg));
					msg.msg_iov = vec;
					msg.msg_iovlen = j;
					if (sendmsg(sfd, &msg, 0) < 0) {
						if (errno != EINTR)
							pr_dbg("%s: sendmsg failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
					} else {
						msgs += j;
					}
					stress_bogo_inc(args);
				} while (LIKELY(stress_continue(args) && (k < dccp_msgs)));
				break;
#if defined(HAVE_SENDMMSG)
			case DCCP_OPT_SENDMMSG:
				do {
					for (j = 0, i = 16; i < sizeof(buf); i += 16, j++, k++) {
						vec[j].iov_base = buf;
						vec[j].iov_len = i;
					}
					(void)shim_memset(msgvec, 0, sizeof(msgvec));
					for (i = 0; i < MSGVEC_SIZE; i++) {
						msgvec[i].msg_hdr.msg_iov = vec;
						msgvec[i].msg_hdr.msg_iovlen = j;
					}
					if (sendmmsg(sfd, msgvec, MSGVEC_SIZE, 0) < 0) {
						if (errno != EINTR)
							pr_dbg("%s: sendmmsg failed, errno=%d (%s)\n",
								args->name, errno, strerror(errno));
					} else {
						msgs += (MSGVEC_SIZE * j);
					}
					stress_bogo_inc(args);
				} while (LIKELY(stress_continue(args) && (k < dccp_msgs)));
				break;
#endif
			default:
				/* Should never happen */
				pr_err("%s: bad option %d\n", args->name, dccp_opts);
				(void)close(sfd);
				goto die_close;
			}
			if (getpeername(sfd, &saddr, &len) < 0) {
				pr_dbg("%s: getpeername failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
#if defined(SIOCOUTQ)
			{
				int pending;

				(void)ioctl(sfd, SIOCOUTQ, &pending);
			}
#endif
			(void)close(sfd);
		}
	} while (stress_continue(args));

die_close:
	(void)close(fd);
die:
	t2 = stress_time_now();
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && (dccp_domain == AF_UNIX)) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#endif
	pr_dbg("%s: %" PRIu64 " messages sent\n", args->name, msgs);

	dt = t2 - t1;
	if (dt > 0.0)
		stress_metrics_set(args, 0, "messages per sec",
			(double)msgs / dt, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

/*
 *  stress_dccp
 *	stress by heavy dccp  I/O
 */
static int stress_dccp(stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	int dccp_port = DEFAULT_DCCP_PORT;
	int dccp_domain = AF_INET;
	int dccp_opts = DCCP_OPT_SEND;
	int rc = EXIT_SUCCESS, reserved_port, parent_cpu;
	char *dccp_if = NULL;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("dcpp-if", &dccp_if);
	(void)stress_get_setting("dccp-port", &dccp_port);
	(void)stress_get_setting("dccp-domain", &dccp_domain);
	(void)stress_get_setting("dccp-opts", &dccp_opts);

	if (dccp_if) {
		int ret;
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(dccp_if, dccp_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, dccp_if, stress_net_domain(dccp_domain));
			dccp_if = NULL;
		}
	}

	dccp_port += args->instance;
	if (dccp_port > MAX_PORT)
		dccp_port -= (MAX_PORT - MIN_PORT + 1);
	reserved_port = stress_net_reserve_ports(dccp_port, dccp_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, dccp_port);
		return EXIT_NO_RESOURCE;
	}
	dccp_port = reserved_port;

	pr_dbg("%s: process [%d] using socket port %d\n",
		args->name, (int)args->pid, dccp_port);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_dbg("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		(void)sched_settings_apply(true);
		rc = stress_dccp_client(args, mypid, dccp_port, dccp_domain, dccp_if);
		_exit(rc);
	} else {
		rc = stress_dccp_server(args, mypid, dccp_port,
			dccp_domain, dccp_if, dccp_opts);

		(void)stress_kill_pid_wait(pid, NULL);
	}
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(dccp_port, dccp_port);
	return rc;
}

const stressor_info_t stress_dccp_info = {
	.stressor = stress_dccp,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_dccp_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without IPPROTO_DCCP or SOCK_DCCP defined"
};
#endif
