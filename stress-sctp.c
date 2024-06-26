/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2024 Colin Ian King.
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

#if defined(HAVE_SYS_UN_H)
#include <sys/un.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_SCTP_H)
#include <netinet/sctp.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_NETINET_TCP_H)
#include <netinet/tcp.h>
#else
UNEXPECTED
#endif

#define MIN_SCTP_PORT		(1024)
#define MAX_SCTP_PORT		(65535)
#define DEFAULT_SCTP_PORT	(9000)

#define SOCKET_BUF		(8192)	/* Socket I/O buffer size */

typedef struct {
	const int	sched_type;
	const char 	*name;
} stress_sctp_sched_t;

static const stress_help_t help[] = {
	{ NULL,	"sctp N",	 "start N workers performing SCTP send/receives " },
	{ NULL,	"sctp-domain D", "specify sctp domain, default is ipv4" },
	{ NULL,	"sctp-if I",	 "use network interface I, e.g. lo, eth0, etc." },
	{ NULL,	"sctp-ops N",	 "stop after N SCTP bogo operations" },
	{ NULL,	"sctp-port P",	 "use SCTP ports P to P + number of workers - 1" },
	{ NULL, "sctp-sched S",	 "specify sctp scheduler" },
	{ NULL,	NULL, 		 NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#if !defined(LOCALTIME_STREAM)
#define LOCALTIME_STREAM        0
#endif

static uint64_t	sigpipe_count;
#else
UNEXPECTED
#endif

/*
 *  stress_set_sctp_port()
 *	set port to use
 */
static int stress_set_sctp_port(const char *opt)
{
	int sctp_port;

	stress_set_net_port("sctp-port", opt,
		MIN_SCTP_PORT, MAX_SCTP_PORT - STRESS_PROCS_MAX,
		&sctp_port);
	return stress_set_setting("sctp-port", TYPE_ID_INT, &sctp_port);
}

/*
 *  stress_set_sctp_sched()
 *	set scheduler to use
 */
static int stress_set_sctp_sched(const char *opt)
{
#if defined(HAVE_SCTP_SCHED_TYPE) && 	\
    defined(HAVE_SCTP_ASSOC_VALUE) &&	\
    (defined(SCTP_SS_FCFS) ||		\
     defined(SCTP_SS_PRIO) ||		\
     defined(SCTP_SS_RR))
	size_t i;

	static const stress_sctp_sched_t stress_sctp_scheds[] = {
		{ (int)SCTP_SS_FCFS,	"fcfs" },
		{ (int)SCTP_SS_PRIO,	"prio" },
		{ (int)SCTP_SS_RR,	"rr", },
	};

	for (i = 0; i < SIZEOF_ARRAY(stress_sctp_scheds); i++) {
		if (!strcmp(opt, stress_sctp_scheds[i].name)) {
			return stress_set_setting("sctp-sched", TYPE_ID_INT, &stress_sctp_scheds[i].sched_type);
		}
	}
	(void)fprintf(stderr, "sctp-sched must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_sctp_scheds); i++) {
                (void)fprintf(stderr, " %s", stress_sctp_scheds[i].name);
        }
        (void)fprintf(stderr, "\n");
	return -1;
#else
	(void)opt;
	pr_inf("sctp-sched option ignored, no SCTP scheduler types available\n");
	return 0;
#endif
}

/*
 *  stress_set_sctp_domain()
 *	set the socket domain option
 */
static int stress_set_sctp_domain(const char *name)
{
	int ret, sctp_domain;

	ret = stress_set_net_domain(DOMAIN_INET | DOMAIN_INET6, "sctp-domain",
				     name, &sctp_domain);
	stress_set_setting("sctp-domain", TYPE_ID_INT, &sctp_domain);

	return ret;
}

static int stress_set_sctp_if(const char *name)
{
        return stress_set_setting("sctp-if", TYPE_ID_STR, name);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sctp_domain,	stress_set_sctp_domain },
	{ OPT_sctp_if,		stress_set_sctp_if },
	{ OPT_sctp_port,	stress_set_sctp_port },
	{ OPT_sctp_sched,	stress_set_sctp_sched },
	{ 0,			NULL }
};

#if defined(HAVE_LIB_SCTP) &&	\
    defined(HAVE_NETINET_SCTP_H)

#define STRESS_SCTP_SOCKOPT(opt, type)			\
{							\
	type info;					\
	socklen_t opt_len = sizeof(info);		\
	int ret;					\
							\
	ret = getsockopt(fd, IPPROTO_SCTP, opt,		\
		 &info, &opt_len);			\
	if (ret == 0) {					\
		VOID_RET(int, setsockopt(fd,		\
			IPPROTO_SCTP, opt, &info,	\
			opt_len));			\
	}						\
}

/*
 *  stress_sctp_sockopts()
 *	exercise some SCTP specific sockopts
 */
static void stress_sctp_sockopts(const int fd)
{
#if defined(SCTP_RTOINFO) && 	\
    defined(HAVE_SCTP_RTOINFO)
	STRESS_SCTP_SOCKOPT(SCTP_RTOINFO, struct sctp_rtoinfo)
#else
	UNEXPECTED
#endif
#if defined(SCTP_ASSOCINFO) &&	\
    defined(HAVE_SCTP_ASSOCPARAMS)
	STRESS_SCTP_SOCKOPT(SCTP_ASSOCINFO, struct sctp_assocparams)
#else
	UNEXPECTED
#endif
#if defined(SCTP_INITMSG) &&	\
    defined(HAVE_SCTP_INITMSG)
	STRESS_SCTP_SOCKOPT(SCTP_INITMSG, struct sctp_initmsg)
#else
	UNEXPECTED
#endif
#if defined(SCTP_NODELAY)
	STRESS_SCTP_SOCKOPT(SCTP_NODELAY, int)
#else
	UNEXPECTED
#endif
#if defined(SCTP_PRIMARY_ADDR) &&	\
    defined(HAVE_SCTP_PRIM)
	STRESS_SCTP_SOCKOPT(SCTP_PRIMARY_ADDR, struct sctp_prim)
#else
	UNEXPECTED
#endif
#if defined(SCTP_PEER_ADDR_PARAMS) &&	\
    defined(HAVE_SCTP_PADDRPARAMS)
	STRESS_SCTP_SOCKOPT(SCTP_PEER_ADDR_PARAMS, struct sctp_paddrparams)
#else
	UNEXPECTED
#endif
#if defined(SCTP_EVENTS) &&	\
    defined(HAVE_SCTP_EVENT_SUBSCRIBE)
	STRESS_SCTP_SOCKOPT(SCTP_EVENTS, struct sctp_event_subscribe)
#else
	UNEXPECTED
#endif
#if defined(SCTP_MAXSEG)
	{
		static bool once = false;

		if (once == false) {
			STRESS_SCTP_SOCKOPT(SCTP_MAXSEG, int)
			once = true;
		}
	}
#if defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_MAXSEG, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#else
	UNEXPECTED
#endif
#if defined(SCTP_STATUS) && 	\
    defined(HAVE_SCTP_STATUS)
	STRESS_SCTP_SOCKOPT(SCTP_STATUS, struct sctp_status)
#else
	UNEXPECTED
#endif
#if defined(SCTP_GET_PEER_ADDR_INFO) &&	\
    defined(HAVE_SCTP_PADDRINFO)
	STRESS_SCTP_SOCKOPT(SCTP_GET_PEER_ADDR_INFO, struct sctp_paddrinfo)
#else
	UNEXPECTED
#endif
#if defined(SCTP_GET_ASSOC_STATS) &&	\
    defined(HAVE_SCTP_ASSOC_STATS)
	STRESS_SCTP_SOCKOPT(SCTP_GET_ASSOC_STATS, struct sctp_assoc_stats)
#else
	UNEXPECTED
#endif
#if defined(SCTP_MAX_BURST)
	{
		static bool once = false;

		if (once == false) {
			STRESS_SCTP_SOCKOPT(SCTP_MAX_BURST, unsigned int)
			once = true;
		}
	}
#if defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_MAX_BURST, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#endif
#if defined(SCTP_AUTOCLOSE)
	STRESS_SCTP_SOCKOPT(SCTP_AUTOCLOSE, unsigned int)
#else
	UNEXPECTED
#endif
#if defined(SCTP_GET_PEER_ADDRS) &&	\
    defined(HAVE_SCTP_GETADDRS)
	STRESS_SCTP_SOCKOPT(SCTP_GET_PEER_ADDRS, struct sctp_getaddrs)
#else
	UNEXPECTED
#endif
#if defined(SCTP_GET_LOCAL_ADDRS) &&	\
    defined(HAVE_SCTP_GETADDRS)
	STRESS_SCTP_SOCKOPT(SCTP_GET_LOCAL_ADDRS, struct sctp_getaddrs)
#else
	UNEXPECTED
#endif
#if defined(SCTP_ADAPTATION_LAYER) &&	\
    defined(HAVE_SCTP_SETADAPTION)
	STRESS_SCTP_SOCKOPT(SCTP_ADAPTATION_LAYER, struct sctp_setadaptation)
#else
	UNEXPECTED
#endif
#if defined(SCTP_CONTEXT) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_CONTEXT, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_FRAGMENT_INTERLEAVE)
	STRESS_SCTP_SOCKOPT(SCTP_FRAGMENT_INTERLEAVE, int)
#else
	UNEXPECTED
#endif
#if defined(SCTP_PARTIAL_DELIVERY_POINT)
	STRESS_SCTP_SOCKOPT(SCTP_PARTIAL_DELIVERY_POINT, uint32_t)
#else
	UNEXPECTED
#endif
#if defined(SCTP_AUTO_ASCONF)
	STRESS_SCTP_SOCKOPT(SCTP_AUTO_ASCONF, int)
#else
	UNEXPECTED
#endif
#if defined(SCTP_DEFAULT_PRINFO) &&	\
    defined(HAVE_SCTP_DEFAULT_PRINFO)
	STRESS_SCTP_SOCKOPT(SCTP_DEFAULT_PRINFO, struct sctp_default_prinfo)
#else
	UNEXPECTED
#endif
#if defined(SCTP_STREAM_SCHEDULER) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_STREAM_SCHEDULER, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_STREAM_SCHEDULER_VALUE) && \
    defined(HAVE_SCTP_STREAM_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_STREAM_SCHEDULER_VALUE, struct sctp_stream_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_INTERLEAVING_SUPPORTED) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_INTERLEAVING_SUPPORTED, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_REUSE_PORT) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_REUSE_PORT, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_EVENT) &&	\
    defined(HAVE_SCTP_EVENT_SUBSCRIBE)
	STRESS_SCTP_SOCKOPT(SCTP_EVENT, struct sctp_event_subscribe)
#endif
#if defined(SCTP_ASCONF_SUPPORTED) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_ASCONF_SUPPORTED, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_AUTH_SUPPORTED) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_AUTH_SUPPORTED, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_ECN_SUPPORTED) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_ECN_SUPPORTED, struct sctp_assoc_value)
#else
	UNEXPECTED
#endif
#if defined(SCTP_EXPOSE_POTENTIALLY_FAILED_STATE) && 	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	STRESS_SCTP_SOCKOPT(SCTP_EXPOSE_POTENTIALLY_FAILED_STATE, struct sctp_assoc_value)
#endif
#if defined(SCTP_REMOTE_UDP_ENCAPS_PORT) && 	\
    defined(HAVE_SCTP_ASSOCIATION)
	STRESS_SCTP_SOCKOPT(SCTP_REMOTE_UDP_ENCAPS_PORT, struct sctp_association)
#else
	//UNEXPECTED
#endif
#if defined(SCTP_PLPMTUD_PROBE_INTERVAL) &&	\
    defined(HAVE_SCTP_PROBEINTERVAL)
	STRESS_SCTP_SOCKOPT(SCTP_PLPMTUD_PROBE_INTERVAL, struct sctp_probeinterval)
#else
	UNEXPECTED
#endif
}

/*
 *  stress_sctp_client()
 *	client reader
 */
static int OPTIMIZE3 stress_sctp_client(
	stress_args_t *args,
	const pid_t mypid,
	const int sctp_port,
	const int sctp_domain,
	const int sctp_sched,
	const char *sctp_if)
{
	struct sockaddr *addr;
	int rc = EXIT_SUCCESS;

	(void)sctp_sched;

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	do {
		char ALIGN64 buf[SOCKET_BUF];
		int fd;
		int retries = 0;
		socklen_t addr_len = 0;
#if defined(HAVE_SCTP_EVENT_SUBSCRIBE)
		struct sctp_event_subscribe events;
#endif
retry:
		if (!stress_continue_flag())
			return EXIT_FAILURE;
		fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP);
		if (UNLIKELY(fd < 0)) {
			if (errno == EPROTONOSUPPORT) {
				if (args->instance == 0)
					pr_inf_skip("%s: SCTP protocol not supported, skipping stressor\n",
						args->name);
				return EXIT_NO_RESOURCE;
			}
			pr_fail("%s: socket failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}

		if (UNLIKELY(stress_set_sockaddr_if(args->name, args->instance, mypid,
				sctp_domain, sctp_port, sctp_if,
				&addr, &addr_len, NET_ADDR_LOOPBACK) < 0)) {
			(void)close(fd);
			return EXIT_FAILURE;
		}
		if (UNLIKELY(connect(fd, addr, addr_len) < 0)) {
			const int save_errno = errno;

			(void)close(fd);
			(void)shim_usleep(10000);
			retries++;
			if (retries > 100) {
				/* Give up.. */
				pr_fail("%s: connect failed after 100 retries, errno=%d (%s)\n",
					args->name, save_errno, strerror(save_errno));
				return EXIT_FAILURE;
			}
			goto retry;
		}
#if defined(HAVE_SCTP_EVENT_SUBSCRIBE)
		(void)shim_memset(&events, 0, sizeof(events));
		events.sctp_data_io_event = 1;
		if (UNLIKELY(setsockopt(fd, SOL_SCTP, SCTP_EVENTS, &events, sizeof(events)) < 0)) {
			(void)close(fd);
			pr_fail("%s: setsockopt failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
#endif
#if defined(HAVE_SCTP_SCHED_TYPE) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
		if (sctp_sched > -1) {
			struct sctp_assoc_value val;

			(void)shim_memset(&val, 0, sizeof(val));
			val.assoc_value = (uint32_t)sctp_sched;
			(void)setsockopt(fd, SOL_SCTP, SCTP_STREAM_SCHEDULER, &val, sizeof(val));
		}
#endif

		do {
			int flags;
			struct sctp_sndrcvinfo sndrcvinfo;
			ssize_t n;

			n = sctp_recvmsg(fd, buf, sizeof(buf),
				NULL, 0, &sndrcvinfo, &flags);
			if (UNLIKELY(n <= 0))
				break;
			if (n >= (ssize_t)sizeof(pid_t)) {
				const pid_t *pidptr = (pid_t *)buf;
				const pid_t pid = *pidptr;

				if (UNLIKELY(pid != mypid)) {
					pr_fail("%s: server received unexpected data "
						"contents, got 0x%" PRIxMAX ", "
						"expected 0x%" PRIxMAX "\n",
						args->name, (intmax_t)pid,
						(intmax_t)mypid);
					rc = EXIT_FAILURE;
					break;
				}
			}
		} while (stress_continue_flag());
		(void)shutdown(fd, SHUT_RDWR);
		(void)close(fd);
	} while (stress_continue(args));

#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (sctp_domain == AF_UNIX) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	return rc;
}

/*
 *  stress_sctp_server()
 *	server writer
 */
static int OPTIMIZE3 stress_sctp_server(
	stress_args_t *args,
	const pid_t mypid,
	const int sctp_port,
	const int sctp_domain,
	const int sctp_sched,
	const char *sctp_if)
{
	char ALIGN64 buf[SOCKET_BUF];
	int fd;
	int so_reuseaddr = 1;
	socklen_t addr_len = 0;
	struct sockaddr *addr = NULL;
	int rc = EXIT_SUCCESS;
	int idx = 0;

	(void)sctp_sched;

	if (stress_sig_stop_stressing(args->name, SIGALRM)) {
		rc = EXIT_FAILURE;
		goto die;
	}
	fd = socket(sctp_domain, SOCK_STREAM, IPPROTO_SCTP);
	if (fd < 0) {
		if (errno == EPROTONOSUPPORT) {
			if (args->instance == 0)
				pr_inf_skip("%s: SCTP protocol not supported, skipping stressor\n",
					args->name);
			rc = EXIT_NO_RESOURCE;
			goto die;
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
		sctp_domain, sctp_port, sctp_if, &addr, &addr_len, NET_ADDR_ANY) < 0) {
		rc = EXIT_FAILURE;
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
#if defined(TCP_NODELAY)
	{
		int one = 1;

		if (g_opt_flags & OPT_FLAGS_SOCKET_NODELAY) {
			if (setsockopt(fd, SOL_TCP, TCP_NODELAY, &one, sizeof(one)) < 0) {
				pr_inf("%s: setsockopt TCP_NODELAY "
					"failed and disabled, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				g_opt_flags &= ~OPT_FLAGS_SOCKET_NODELAY;
			}
		}
	}
#else
	UNEXPECTED
#endif

#if defined(HAVE_SCTP_SCHED_TYPE) &&	\
    defined(HAVE_SCTP_ASSOC_VALUE)
	if (sctp_sched > -1) {
		struct sctp_assoc_value val;

		(void)shim_memset(&val, 0, sizeof(val));
		val.assoc_value = (uint32_t)sctp_sched;
		(void)setsockopt(fd, SOL_SCTP, SCTP_STREAM_SCHEDULER, &val, sizeof(val));
	}
#endif
	do {
		int sfd;

		if (UNLIKELY(!stress_continue(args)))
			break;

		sfd = accept(fd, (struct sockaddr *)NULL, NULL);
		if (LIKELY(sfd >= 0)) {
			size_t i;
			const int c = stress_ascii32[idx++ & 0x1f];
			pid_t *pidptr = (pid_t *)buf;

			(void)shim_memset(buf, c, sizeof(buf));
			*pidptr = mypid;
			for (i = 16; i < sizeof(buf); i += 16) {
				ssize_t ret = sctp_sendmsg(sfd, buf, i,
						NULL, 0, 0, 0,
						LOCALTIME_STREAM, 0, 0);
				if (UNLIKELY(ret < 0))
					break;

				stress_bogo_inc(args);
			}
			stress_sctp_sockopts(sfd);
			(void)close(sfd);
		}
	} while (stress_continue(args));

die_close:
	(void)close(fd);
die:
#if defined(AF_UNIX) &&		\
    defined(HAVE_SOCKADDR_UN)
	if (addr && (sctp_domain == AF_UNIX)) {
		const struct sockaddr_un *addr_un = (struct sockaddr_un *)addr;

		(void)shim_unlink(addr_un->sun_path);
	}
#else
	UNEXPECTED
#endif
	return rc;
}

static void stress_sctp_sigpipe(int signum)
{
	(void)signum;

	sigpipe_count++;
}

/*
 *  stress_sctp
 *	stress SCTP by heavy SCTP network I/O
 */
static int stress_sctp(stress_args_t *args)
{
	pid_t pid, mypid = getpid();
	int sctp_port = DEFAULT_SCTP_PORT;
	int sctp_domain = AF_INET;
	int sctp_sched = -1;	/* Undefined */
	int ret, reserved_port, parent_cpu;
	char *sctp_if = NULL;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("sctp-domain", &sctp_domain);
	(void)stress_get_setting("sctp-if", &sctp_if);
	(void)stress_get_setting("sctp-port", &sctp_port);
	(void)stress_get_setting("sctp-sched", &sctp_sched);

	if (sctp_if) {
		struct sockaddr if_addr;

		ret = stress_net_interface_exists(sctp_if, sctp_domain, &if_addr);
		if (ret < 0) {
			pr_inf("%s: interface '%s' is not enabled for domain '%s', defaulting to using loopback\n",
				args->name, sctp_if, stress_net_domain(sctp_domain));
			sctp_if = NULL;
		}
	}

	if (stress_sighandler(args->name, SIGPIPE, stress_sctp_sigpipe, NULL) < 0)
		return EXIT_FAILURE;

	sctp_port += args->instance;
	reserved_port = stress_net_reserve_ports(sctp_port, sctp_port);
	if (reserved_port < 0) {
		pr_inf_skip("%s: cannot reserve port %d, skipping stressor\n",
			args->name, sctp_port);
		return EXIT_NO_RESOURCE;
	}
	sctp_port = reserved_port;

	pr_dbg("%s: process [%" PRIdMAX "] using socket port %d\n",
		args->name, (intmax_t)args->pid, sctp_port);

	ret = EXIT_FAILURE;
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);
again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args))
			goto finish;
		pr_fail("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		 (void)stress_change_cpu(args, parent_cpu);
		ret = stress_sctp_client(args, mypid, sctp_port, sctp_domain, sctp_sched, sctp_if);
		_exit(ret);
	} else {
		int status;

		ret = stress_sctp_server(args, mypid, sctp_port, sctp_domain, sctp_sched, sctp_if);
		(void)stress_kill_pid_wait(pid, &status);
		if (WIFEXITED(status)) {
			if (WEXITSTATUS(status) != EXIT_SUCCESS) {
				ret = WEXITSTATUS(status);
			}
		}
	}

finish:
	if (sigpipe_count)
		pr_dbg("%s: caught %" PRIu64 " SIGPIPE signals\n", args->name, sigpipe_count);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_net_release_ports(sctp_port, sctp_port);

	return ret;
}

stressor_info_t stress_sctp_info = {
	.stressor = stress_sctp,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sctp_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_NETWORK,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without netinet/sctp.h or libsctp support"
};
#endif
