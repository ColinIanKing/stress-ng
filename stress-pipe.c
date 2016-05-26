/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

#if defined(F_SETPIPE_SZ)
static size_t opt_pipe_size = 0;
#endif
static size_t opt_pipe_data_size = 512;

#define PIPE_STOP	"PS!"

#if defined(F_SETPIPE_SZ)
/*
 *  stress_set_pipe_size()
 *	set pipe size in bytes
 */
void stress_set_pipe_size(const char *optarg)
{
        opt_pipe_size = (size_t)get_uint64_byte(optarg);
        check_range("pipe-size", opt_pipe_size,
               4, 1024 * 1024);
}
#endif

/*
 *  stress_set_pipe_size()
 *	set pipe data write size in bytes
 */
void stress_set_pipe_data_size(const char *optarg)
{
        opt_pipe_data_size = (size_t)get_uint64_byte(optarg);
        check_range("pipe-data_size", opt_pipe_data_size,
                4, stress_get_pagesize());
}

/*
 *  pipe_memset()
 *	set pipe data to be incrementing chars from val upwards
 */
static inline void pipe_memset(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		*buf++ = val++;
}

/*
 *  pipe_memchk()
 *	check pipe data contains incrementing chars from val upwards
 */
static inline int pipe_memchk(char *buf, char val, const size_t sz)
{
	size_t i;

	for (i = 0; i < sz; i++)
		if (*buf++ != val++)
			return 1;
	return 0;
}

#if defined(F_SETPIPE_SZ)
/*
 *  pipe_change_size()
 *	see if we can change the pipe size
 */
static void pipe_change_size(const char *name, const int fd)
{
#if defined(F_GETPIPE_SZ)
	ssize_t sz;
#endif
	if (!opt_pipe_size)
		return;

#if !(defined(__linux__) && NEED_GLIBC(2,9,0))
	if (opt_pipe_size < stress_get_pagesize())
		return;
#endif
	if (fcntl(fd, F_SETPIPE_SZ, opt_pipe_size) < 0) {
		pr_err(stderr, "%s: cannot set pipe size, keeping "
			"default pipe size, errno=%d (%s)\n",
			name, errno, strerror(errno));
	}
#if defined(F_GETPIPE_SZ)
	/* Sanity check size */
	if ((sz = fcntl(fd, F_GETPIPE_SZ)) < 0) {
		pr_err(stderr, "%s: cannot get pipe size, errno=%d (%s)\n",
			name, errno, strerror(errno));
	} else {
		if ((size_t)sz != opt_pipe_size) {
			pr_err(stderr, "%s: cannot set desired pipe size, "
				"pipe size=%zd, errno=%d (%s)\n",
				name, sz, errno, strerror(errno));
		}
	}
#endif
}
#endif


/*
 *  stress_pipe
 *	stress by heavy pipe I/O
 */
int stress_pipe(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	int pipefds[2];

	(void)instance;

#if defined(__linux__) && NEED_GLIBC(2,9,0)
	if (pipe2(pipefds, O_DIRECT) < 0) {
		pr_fail_dbg(name, "pipe2");
		return EXIT_FAILURE;
	}
#else
	if (pipe(pipefds) < 0) {
		pr_fail_dbg(name, "pipe");
		return EXIT_FAILURE;
	}
#endif

#if defined(F_SETPIPE_SZ)
	pipe_change_size(name, pipefds[0]);
	pipe_change_size(name, pipefds[1]);
#endif

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		(void)close(pipefds[0]);
		(void)close(pipefds[1]);
		pr_fail_dbg(name, "fork");
		return EXIT_FAILURE;
	} else if (pid == 0) {
		int val = 0;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		(void)close(pipefds[1]);
		while (opt_do_run) {
			char buf[opt_pipe_data_size];
			ssize_t n;

			n = read(pipefds[0], buf, opt_pipe_data_size);
			if (n <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail_dbg(name, "read");
					break;
				}
				pr_fail_dbg(name, "zero byte read");
				break;
			}
			if (!strcmp(buf, PIPE_STOP))
				break;
			if ((opt_flags & OPT_FLAGS_VERIFY) &&
			    pipe_memchk(buf, val++, (size_t)n)) {
				pr_fail(stderr, "%s: pipe read error detected, "
					"failed to read expected data\n", name);
			}
		}
		(void)close(pipefds[0]);
		exit(EXIT_SUCCESS);
	} else {
		char buf[opt_pipe_data_size];
		int val = 0, status;

		/* Parent */
		setpgid(pid, pgrp);
		(void)close(pipefds[0]);

		do {
			ssize_t ret;

			pipe_memset(buf, val++, opt_pipe_data_size);
			ret = write(pipefds[1], buf, opt_pipe_data_size);
			if (ret <= 0) {
				if ((errno == EAGAIN) || (errno == EINTR))
					continue;
				if (errno) {
					pr_fail_dbg(name, "write");
					break;
				}
				continue;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		strncpy(buf, PIPE_STOP, opt_pipe_data_size);
		if (write(pipefds[1], buf, sizeof(buf)) <= 0) {
			if (errno != EPIPE)
				pr_fail_dbg(name, "termination write");
		}
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
		(void)close(pipefds[1]);
	}
	return EXIT_SUCCESS;
}
