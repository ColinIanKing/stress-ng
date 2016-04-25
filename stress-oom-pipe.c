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

#include "stress-ng.h"

#if defined(STRESS_OOM_PIPE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>

static int page_size;

/*
 *  check_max_pipe_size()
 *	check if the given pipe size is allowed
 */
static int check_max_pipe_size(const int sz)
{
	int fds[2];

	if (sz < page_size)
		return -1;

	if (pipe(fds) < 0)
		return -1;

	if (fcntl(fds[0], F_SETPIPE_SZ, sz) < 0)
		return -1;

	(void)close(fds[0]);
	(void)close(fds[1]);
	return 0;
}

/*
 *  probe_max_pipe_size()
 *	determine the maximim allowed pipe size
 */
static int probe_max_pipe_size(void)
{
	int i, ret, prev_sz, sz, min, max;
	char buf[64];

	/*
	 *  Try and find maximum pipe size directly
	 */
	ret = system_read("/proc/sys/fs/pipe-max-size", buf, sizeof(buf));
	if (ret > 0) {
		if (sscanf(buf, "%d", &sz) == 1)
			if (!check_max_pipe_size(sz))
				return sz;
	}

	/*
	 *  Need to find size by binary chop probing
	 */
	min = page_size;
	max = INT_MAX;
	prev_sz = 0;
	for (i = 0; i < 64; i++) {
		sz = min + (max - min) / 2;
		if (prev_sz == sz)
			return sz;
		prev_sz = sz;
		if (check_max_pipe_size(sz) == 0) {
			min = sz;
		} else {
			max = sz;
		}
	}

	return sz;
}

/*
 *  pipe_empty()
 *	read data from read end of pipe
 */
static void pipe_empty(const int fd, const int max)
{
	int i;

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;
		char buffer[page_size];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret < 0)
			return;
	}
}

/*
 *  pipe_fill()
 *	write data to fill write end of pipe
 */
static void pipe_fill(const int fd, const int max)
{
	int i;
	char buffer[page_size];

	memset(buffer, 'X', sizeof(buffer));

	for (i = 0; i < max; i += page_size) {
		ssize_t ret;

		ret = write(fd, buffer, sizeof(buffer));
		if (ret < (ssize_t)sizeof(buffer))
			return;
	}
}

/*
 *  stress_oom_pipe_expander
 *
 */
static int stress_oom_pipe_expander(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name,
	const int max_pipe_size,
	const int max_pipes)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		pr_err(stderr, "%s: fork failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	} else if (pid > 0) {
		int status, ret;

		setpgid(pid, pgrp);
		stress_parent_died_alarm();

		/* Patent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid() errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %s (instance %d)\n",
				name, stress_strsignal(WTERMSIG(status)),
				instance);
			/* If we got kill by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg(stderr, "%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					name, instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		/* Child */
		int fds[max_pipes * 2], *fd, i, pipes_open = 0;
		const bool aggressive = (opt_flags & OPT_FLAGS_AGGRESSIVE);

		setpgid(0, pgrp);
		set_oom_adjustment(name, true);

		for (i = 0; i < max_pipes * 2; i++)
			fds[i] = -1;

		for (i = 0; i < max_pipes; i++) {
			int *fd = fds + (2 * i);
			if (pipe(fd) < 0) {
				fd[0] = -1;
				fd[1] = -1;
			} else {
				if (fcntl(fd[0], F_SETFL, O_NONBLOCK) < 0) {
					pr_fail(stderr, "%s: fcntl O_NONBLOCK failed, "
						"errno = %d (%s)\n",
						name, errno, strerror(errno));
					goto clean;
				}
				if (fcntl(fd[1], F_SETFL, O_NONBLOCK) < 0) {
					pr_fail(stderr, "%s: fcntl O_NONBLOCK failed, "
						"errno = %d (%s)\n",
						name, errno, strerror(errno));
					goto clean;
				}
				pipes_open++;
			}
		}

		if (!pipes_open) {
			pr_dbg(stderr, "%s: failed to open any pipes, aborted\n",
				name);
			exit(EXIT_NO_RESOURCE);
		}

		do {
			/* Set to maximum size */
			for (i = 0, fd = fds; i < max_pipes * 2; i++, fd++) {
				int max_size = max_pipe_size;
				if (*fd < 0)
					continue;
				if (fcntl(*fd, F_SETPIPE_SZ, max_size) < 0)
					max_size = page_size;
				pipe_fill(fd[1], max_size);
				if (!aggressive)
					pipe_empty(fd[0], max_size);
			}
			/* Set to minimum size */
			for (i = 0, fd = fds; i < max_pipes * 2; i++, fd++) {
				if (*fd < 0)
					continue;
				(void)fcntl(*fd, F_SETPIPE_SZ, page_size);
				pipe_fill(fd[1], max_pipe_size);
				if (!aggressive)
					pipe_empty(fd[0], page_size);
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		/* And close the pipes */
clean:
		for (i = 0, fd = fds; i < max_pipes * 2; i++, fd++) {
			if (*fd >= 0)
				(void)close(*fd);
		}
		exit(EXIT_SUCCESS);
	}
	return 0;
}


/*
 *  stress_oom_pipe
 *	stress pipe memory allocation
 */
int stress_oom_pipe(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t max_fd = stress_get_file_limit();
	const size_t max_pipes = max_fd / 2;
	int max_pipe_size;

	page_size = stress_get_pagesize();
	max_pipe_size = probe_max_pipe_size() & ~(page_size - 1);

	return stress_oom_pipe_expander(
		counter, instance, max_ops, name,
		max_pipe_size, max_pipes);
}

#endif
