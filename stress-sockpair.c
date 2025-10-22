/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include <sys/socket.h>

#define MAX_SOCKET_PAIRS	(32768)
#define SOCKET_PAIR_BUF         (4096)	/* Socket pair I/O buffer size */

static const stress_help_t help[] = {
	{ NULL,	"sockpair N",	  "start N workers exercising socket pair I/O activity" },
	{ NULL,	"sockpair-ops N", "stop after N socket pair bogo operations" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  socket_pair_memset()
 *	set data to be incrementing chars from val upwards
 */
static inline void OPTIMIZE3 socket_pair_memset(
	uint8_t *buf,
	uint8_t val,
	const size_t sz)
{
	register uint8_t *ptr;
	register const uint8_t *buf_end = buf + sz;
	register uint8_t checksum = 0;

PRAGMA_UNROLL_N(4)
	for (ptr = buf + 1 ; ptr < buf_end; *ptr++ = val++)
		checksum += val;
	*buf = checksum;
}

/*
 *  socket_pair_memchk()
 *	check data contains incrementing chars from val upwards
 */
static inline int OPTIMIZE3 socket_pair_memchk(
	uint8_t *buf,
	const size_t sz)
{
	register const uint8_t *ptr, *buf_end = buf + sz;
	register uint8_t checksum = 0;

PRAGMA_UNROLL_N(4)
	for (ptr = buf + 1; ptr < buf_end; checksum += *ptr++)
		;

	return !(checksum == *buf);
}

static void socket_pair_close(
	int fds[MAX_SOCKET_PAIRS][2],
	const int max,
	const int which)
{
	int i;

	for (i = 0; i < max; i++)
		(void)close(fds[i][which]);
}

/*
 *  socket_pair_try_leak()
 *	exercise Linux kernel fix:
 * 	(" 7a62ed61367b8fd") af_unix: Fix memory leaks of the whole sk due to OOB skb.
 */
static void socket_pair_try_leak(void)
{
#if defined(MSG_OOB)
	int fds[2];

	if (UNLIKELY(socketpair(AF_UNIX, SOCK_STREAM, 0, fds) < 0))
		return;

	(void)send(fds[0], "0", 1, MSG_OOB);
	(void)send(fds[1], "1", 1, MSG_OOB);

	(void)close(fds[0]);
	(void)close(fds[1]);
#endif
}

/*
 *  stress_sockpair_oomable()
 *	this stressor needs to be oom-able in the parent
 *	and child cases
 */
static int stress_sockpair_oomable(stress_args_t *args, void *context)
{
	pid_t pid;
	static int socket_pair_fds[MAX_SOCKET_PAIRS][2];
	int socket_pair_fds_bad[2];
	int i, max, ret, parent_cpu;
	double t, duration, rate, bytes = 0.0;
	uint64_t low_memory_count = 0;
	const size_t low_mem_size = args->page_size * 32 * args->instances;
	const bool oom_avoid = !!(g_opt_flags & OPT_FLAGS_OOM_AVOID);
	(void)context;

	/* exercise invalid socketpair domain */
	socket_pair_fds_bad[0] = -1;
	socket_pair_fds_bad[1] = -1;
	ret = socketpair(~0, SOCK_STREAM, 0, socket_pair_fds_bad);
	if (UNLIKELY(ret == 0)) {
		(void)close(socket_pair_fds_bad[0]);
		(void)close(socket_pair_fds_bad[1]);
	}

	/* exercise invalid socketpair type domain */
	socket_pair_fds_bad[0] = -1;
	socket_pair_fds_bad[1] = -1;
	ret = socketpair(AF_UNIX, ~0, 0, socket_pair_fds_bad);
	if (UNLIKELY(ret == 0)) {
		(void)close(socket_pair_fds_bad[0]);
		(void)close(socket_pair_fds_bad[1]);
	}

	/* exercise invalid socketpair type protocol */
	ret = socketpair(AF_UNIX, SOCK_STREAM, ~0, socket_pair_fds_bad);
	if (UNLIKELY(ret == 0)) {
		(void)close(socket_pair_fds_bad[0]);
		(void)close(socket_pair_fds_bad[1]);
	}

	(void)shim_memset(socket_pair_fds, 0, sizeof(socket_pair_fds));
	errno = 0;

	t = stress_time_now();
	for (max = 0; max < MAX_SOCKET_PAIRS; max++) {
		if (UNLIKELY(!stress_continue(args))) {
			socket_pair_close(socket_pair_fds, max, 0);
			socket_pair_close(socket_pair_fds, max, 1);
			return EXIT_SUCCESS;
		}
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_fds[max]) < 0)
			break;
	}
	duration = stress_time_now() - t;
	rate = (duration > 0.0) ? (double)max / duration : 0.0;
	stress_metrics_set(args, 0, "socketpair calls sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	if (max == 0) {
		int rc;

		switch (errno) {
		case EAFNOSUPPORT:
			if (stress_instance_zero(args))
				pr_inf_skip("%s: socketpair: address family not supported, "
					"skipping stressor\n", args->name);
			rc = EXIT_NO_RESOURCE;
			break;
		case EMFILE:
		case ENFILE:
			pr_inf("%s: socketpair: out of file descriptors\n",
				args->name);
			rc = EXIT_NO_RESOURCE;
			break;
		case EPROTONOSUPPORT:
			if (stress_instance_zero(args))
				pr_inf_skip("%s: socketpair: protocol not supported, "
					"skipping stressor\n", args->name);
			rc = EXIT_NO_RESOURCE;
			break;
		case EOPNOTSUPP:
			if (stress_instance_zero(args))
				pr_inf_skip("%s: socketpair: protocol does not support "
					"socket pairs, skipping stressor\n", args->name);
			rc = EXIT_NO_RESOURCE;
			break;
		default:
			pr_fail("%s: socketpair failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
		}
		socket_pair_close(socket_pair_fds, max, 0);
		socket_pair_close(socket_pair_fds, max, 1);
		return rc;
	}

again:
	parent_cpu = stress_get_cpu();
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;

		socket_pair_close(socket_pair_fds, max, 0);
		socket_pair_close(socket_pair_fds, max, 1);

		if (UNLIKELY(!stress_continue(args)))
			goto finish;
		pr_err("%s: fork failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	} else if (pid == 0) {
		const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		(void)stress_change_cpu(args, parent_cpu);
		stress_set_oom_adjustment(args, true);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		socket_pair_close(socket_pair_fds, max, 1);
		while (stress_continue(args)) {
			uint8_t buf[SOCKET_PAIR_BUF] ALIGN64;
			ssize_t n;

			for (i = 0; LIKELY(stress_continue(args) && (i < max)); i++) {
				errno = 0;

				n = read(socket_pair_fds[i][0], buf, sizeof(buf));
				if (UNLIKELY(n <= 0)) {
					switch (errno) {
					case 0:		/* OKAY */
					case EAGAIN:	/* Redo */
					case EINTR:	/* Interrupted */
						continue;
					case ENFILE:	/* Too many files */
					case EMFILE:	/* Occurs on socket shutdown */
					case EPERM:	/* Occurs on socket closure */
					case EPIPE:	/* Pipe broke */
						goto abort;
					default:
						pr_fail("%s: read failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						goto abort;
					}
				}
				if (UNLIKELY(verify && socket_pair_memchk(buf, (size_t)n))) {
					pr_fail("%s: socket_pair read error detected, "
						"failed to read expected data\n", args->name);
				}
				if (UNLIKELY(oom_avoid && stress_low_memory(low_mem_size)))
					continue;
				socket_pair_try_leak();
			}
		}
abort:
		socket_pair_close(socket_pair_fds, max, 0);
		_exit(EXIT_SUCCESS);
	} else {
		uint8_t buf[SOCKET_PAIR_BUF] ALIGN64;
		int val = 0;

		stress_set_oom_adjustment(args, true);
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		/* Parent */
		socket_pair_close(socket_pair_fds, max, 0);

		do {
			for (i = 0; LIKELY(stress_continue(args) && (i < max)); i++) {
				ssize_t wret;

				/* Low memory avoidance, re-start */
				if (UNLIKELY(oom_avoid)) {
					while (stress_low_memory(low_mem_size)) {
						low_memory_count++;
						if (UNLIKELY(!stress_continue_flag()))
							goto tidy;
						(void)shim_usleep(100000);
					}
				}

				socket_pair_memset(buf, (uint8_t)val++, sizeof(buf));
				t = stress_time_now();
				wret = write(socket_pair_fds[i][1], buf, sizeof(buf));
				if (LIKELY(wret > 0)) {
					bytes += (double)wret;
					duration += stress_time_now() - t;
				} else {
					if (errno == EPIPE)
						break;
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno) {
						pr_fail("%s: write failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						break;
					}
					continue;
				}
				(void)shim_sched_yield();
				stress_bogo_inc(args);
			}
		} while (stress_continue(args));

tidy:
		rate = (duration > 0.0) ? (double)bytes / duration : 0.0;
		stress_metrics_set(args, 1, "MB written per sec",
			rate / (double)MB, STRESS_METRIC_HARMONIC_MEAN);

		if (low_memory_count > 0) {
			pr_dbg("%s: %.2f%% of writes backed off due to low memory\n",
				args->name, 100.0 * (double)low_memory_count / (double)stress_bogo_get(args));
		}

		for (i = 0; i < max; i++) {
			if (shutdown(socket_pair_fds[i][1], SHUT_RDWR) < 0)
				if (errno != ENOTCONN) {
					pr_fail("%s: shutdown failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				}
		}
		(void)stress_kill_pid_wait(pid, NULL);
		socket_pair_close(socket_pair_fds, max, 1);
	}
finish:

	return EXIT_SUCCESS;
}

/*
 *  stress_sockpair
 *	stress by heavy socket_pair I/O
 */
static int stress_sockpair(stress_args_t *args)
{
	int rc;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (stress_sighandler(args->name, SIGPIPE, stress_sighandler_nop, NULL) < 0)
		return EXIT_NO_RESOURCE;

	rc = stress_oomable_child(args, NULL, stress_sockpair_oomable, STRESS_OOMABLE_DROP_CAP);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_sockpair_info = {
	.stressor = stress_sockpair,
	.classifier = CLASS_NETWORK | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
