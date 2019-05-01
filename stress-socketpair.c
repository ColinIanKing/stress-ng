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

#define MAX_SOCKET_PAIRS	(32768)

static const help_t help[] = {
	{ NULL,	"sockpair N",	  "start N workers exercising socket pair I/O activity" },
	{ NULL,	"sockpair-ops N", "stop after N socket pair bogo operations" },
	{ NULL,	NULL,		  NULL }
};

/*
 *  socket_pair_memset()
 *	set data to be incrementing chars from val upwards
 */
static inline void socket_pair_memset(
	uint8_t *buf,
	uint8_t val,
	const size_t sz)
{
	register uint8_t *ptr;
	register uint8_t checksum = 0;

	for (ptr = buf + 1 ; ptr < buf + sz; *ptr++ = val++)
		checksum += val;
	*buf = checksum;
}

/*
 *  socket_pair_memchk()
 *	check data contains incrementing chars from val upwards
 */
static inline int socket_pair_memchk(
	uint8_t *buf,
	const size_t sz)
{
	register uint8_t *ptr;
	register uint8_t checksum = 0;

	for (ptr = buf + 1; ptr < buf + sz; checksum += *ptr++)
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
 *  stress_sockpair_oomable()
 *	this stressor needs to be oom-able in the parent
 *	and child cases
 */
static int stress_sockpair_oomable(const args_t *args)
{
	pid_t pid;
	static int socket_pair_fds[MAX_SOCKET_PAIRS][2];
	int i, max;

	for (max = 0; max < MAX_SOCKET_PAIRS; max++) {
		if (socketpair(AF_UNIX, SOCK_STREAM, 0, socket_pair_fds[max]) < 0)
			break;
	}

	if (max == 0) {
		pr_fail_dbg("socket_pair");
		return EXIT_FAILURE;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		socket_pair_close(socket_pair_fds, max, 0);
		socket_pair_close(socket_pair_fds, max, 1);
		pr_fail_dbg("fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		set_oom_adjustment(args->name, true);
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		socket_pair_close(socket_pair_fds, max, 1);
		while (g_keep_stressing_flag) {
			uint8_t buf[SOCKET_PAIR_BUF];
			ssize_t n;

			for (i = 0; g_keep_stressing_flag && (i < max); i++) {
				n = read(socket_pair_fds[i][0], buf, sizeof(buf));
				if (n <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					else if (errno == ENFILE) /* Too many files! */
						goto abort;
					else if (errno == EMFILE) /* Occurs on socket shutdown */
						goto abort;
					else if (errno == EPERM)  /* Occurs on socket closure */
						goto abort;
					else if (errno) {
						pr_fail_dbg("read");
						goto abort;
					}
					continue;
				}
				if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
				    socket_pair_memchk(buf, (size_t)n)) {
					pr_fail("%s: socket_pair read error detected, "
						"failed to read expected data\n", args->name);
				}
			}
		}
abort:
		socket_pair_close(socket_pair_fds, max, 0);
		_exit(EXIT_SUCCESS);
	} else {
		uint8_t buf[SOCKET_PAIR_BUF];
		int val = 0, status;

		(void)setpgid(pid, g_pgrp);
		/* Parent */
		socket_pair_close(socket_pair_fds, max, 0);
		do {
			for (i = 0; keep_stressing() && (i < max); i++) {
				ssize_t ret;

				socket_pair_memset(buf, val++, sizeof(buf));
				ret = write(socket_pair_fds[i][1], buf, sizeof(buf));
				if (ret <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno) {
						pr_fail_dbg("write");
						break;
					}
					continue;
				}
				inc_counter(args);
			}
		} while (keep_stressing());

		for (i = 0; i < max; i++) {
			if (shutdown(socket_pair_fds[i][1], SHUT_RDWR) < 0)
				pr_fail_dbg("socket shutdown");
		}
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
		socket_pair_close(socket_pair_fds, max, 1);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_sockpair
 *	stress by heavy socket_pair I/O
 */
static int stress_sockpair(const args_t *args)
{
	pid_t pid;
	uint32_t restarts = 0;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
	} else if (pid > 0) {
		int status, ret;

		set_oom_adjustment(args->name, false);

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		ret = shim_waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				restarts++;
				goto again;
			}
		}
	 } else if (pid == 0) {
		/* Child, lets do some sockpair stressing... */
		int ret;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		set_oom_adjustment(args->name, true);

		ret = stress_sockpair_oomable(args);
		_exit(ret);
	}

	if (restarts > 0) {
		pr_dbg("%s: OOM restarts: %" PRIu32 "\n",
			args->name, restarts);
	}
	return EXIT_SUCCESS;
}

stressor_info_t stress_sockpair_info = {
	.stressor = stress_sockpair,
	.class = CLASS_NETWORK | CLASS_OS,
	.help = help
};
