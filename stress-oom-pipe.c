/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) && defined(F_SETPIPE_SZ)

static int page_size;

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
static void pipe_fill(const int fd, const size_t max)
{
	size_t i;
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
	const args_t *args,
	const size_t max_pipe_size,
	const int max_pipes)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		pr_err("%s: fork failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -1;
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		stress_parent_died_alarm();

		/* Patent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid() errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg( "%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got kill by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		/* Child */
		int fds[max_pipes * 2], *fd, i, pipes_open = 0;
		const bool aggressive = (g_opt_flags & OPT_FLAGS_AGGRESSIVE);

		(void)setpgid(0, g_pgrp);
		set_oom_adjustment(args->name, true);

		for (i = 0; i < max_pipes * 2; i++)
			fds[i] = -1;

		for (i = 0; i < max_pipes; i++) {
			int *pfd = fds + (2 * i);
			if (pipe(pfd) < 0) {
				pfd[0] = -1;
				pfd[1] = -1;
			} else {
				if (fcntl(pfd[0], F_SETFL, O_NONBLOCK) < 0) {
					pr_fail_err("fcntl O_NONBLOCK");
					goto clean;
				}
				if (fcntl(pfd[1], F_SETFL, O_NONBLOCK) < 0) {
					pr_fail_err("fcntl O_NONBLOCK");
					goto clean;
				}
				pipes_open++;
			}
		}

		if (!pipes_open) {
			pr_dbg("%s: failed to open any pipes, aborted\n",
				args->name);
			exit(EXIT_NO_RESOURCE);
		}

		do {
			/* Set to maximum size */
			for (i = 0, fd = fds; i < max_pipes; i++, fd += 2) {
				size_t max_size = max_pipe_size;

				if ((fd[0] < 0) || (fd[1] < 0))
					continue;
				if (fcntl(fd[0], F_SETPIPE_SZ, max_size) < 0)
					max_size = page_size;
				if (fcntl(fd[1], F_SETPIPE_SZ, max_size) < 0)
					max_size = page_size;
				pipe_fill(fd[1], max_size);
				if (!aggressive)
					pipe_empty(fd[0], max_size);
			}
			/* Set to minimum size */
			for (i = 0, fd = fds; i < max_pipes; i++, fd += 2) {
				if ((fd[0] < 0) || (fd[1] < 0))
					continue;
				(void)fcntl(fd[0], F_SETPIPE_SZ, page_size);
				(void)fcntl(fd[1], F_SETPIPE_SZ, page_size);
				pipe_fill(fd[1], max_pipe_size);
				if (!aggressive)
					pipe_empty(fd[0], page_size);
			}
			inc_counter(args);
		} while (keep_stressing());

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
int stress_oom_pipe(const args_t *args)
{
	const size_t max_fd = stress_get_file_limit();
	const size_t max_pipes = max_fd / 2;
	size_t max_pipe_size;

	page_size = args->page_size;
	max_pipe_size = stress_probe_max_pipe_size() & ~(page_size - 1);

	return stress_oom_pipe_expander(args, max_pipe_size, max_pipes);
}
#else
int stress_oom_pipe(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
