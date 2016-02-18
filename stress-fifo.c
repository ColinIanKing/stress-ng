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
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/select.h>
#include <errno.h>
#include <fcntl.h>

#include "stress-ng.h"

static uint64_t opt_fifo_readers = DEFAULT_FIFO_READERS;
static bool set_fifo_readers = false;

void stress_set_fifo_readers(const char *optarg)
{
	set_fifo_readers = true;
	opt_fifo_readers = get_uint64(optarg);
	check_range("fifo-readers", opt_fifo_readers,
		MIN_FIFO_READERS, MAX_FIFO_READERS);
}

/*
 *  fifo_spawn()
 *	spawn a process
 */
static pid_t fifo_spawn(
	void (*func)(const char *name, const char *fifoname),
	const char *name,
	const char *fifoname)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();
		func(name, fifoname);
		exit(EXIT_SUCCESS);
	}
	setpgid(pid, pgrp);
	return pid;
}

/*
 *  stress_fifo_readers()
 *	read fifo
 */
void stress_fifo_reader(const char *name, const char *fifoname)
{
	int fd;
	uint64_t val, lastval = 0;
	uint64_t wrap_mask = 0xffff000000000000ULL;

	fd = open(fifoname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_err(stderr, "%s: fifo read open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return;
	}
	while (opt_do_run) {
		ssize_t sz;
		int ret;
		struct timeval timeout;
		fd_set rdfds;

		FD_ZERO(&rdfds);
		FD_SET(fd, &rdfds);

		timeout.tv_sec = 10;
		timeout.tv_usec = 0;

		ret = select(fd + 1, &rdfds, NULL, NULL, &timeout);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err(stderr, "%s: select failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		} else if (ret == 0) {
			pr_err(stderr, "%s: read timeout!\n", name);
			break;
		}
		sz = read(fd, &val, sizeof(val));
		if (sz < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err(stderr, "%s: fifo read failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		if (sz == 0)
			break;
		if (sz != sizeof(val)) {
			pr_err(stderr, "%s: fifo read did not get uint64\n",
				name);
			break;
		}
		if ((val < lastval) &&
		    ((~val & wrap_mask) && (lastval & wrap_mask))) {
			pr_err(stderr, "%s: fifo read did not get expected value\n",
				name);
			break;
		}
	}
	(void)close(fd);
}


/*
 *  stress_fifo
 *	stress by heavy fifo I/O
 */
int stress_fifo(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_FIFO_READERS];
	int fd;
	char fifoname[PATH_MAX];
	uint64_t i, val = 0ULL;
	int ret = EXIT_FAILURE;
	const pid_t pid = getpid();

	if (!set_fifo_readers) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fifo_readers = MAX_FIFO_READERS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fifo_readers = MIN_FIFO_READERS;
	}

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(fifoname, sizeof(fifoname),
                name, pid, instance, mwc32());
	(void)umask(0077);

	if (mkfifo(fifoname, S_IRUSR | S_IWUSR) < 0) {
		ret = exit_status(errno);
		pr_err(stderr, "%s: mkfifo failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		goto tidy;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_fifo_readers; i++) {
		pids[i] = fifo_spawn(stress_fifo_reader, name, fifoname);
		if (pids[i] < 0)
			goto reap;
		if (!opt_do_run) {
			ret = EXIT_SUCCESS;
			goto reap;
		}
	}

	fd = open(fifoname, O_WRONLY);
	if (fd < 0) {
		ret = exit_status(fd);
		pr_err(stderr, "%s: fifo write open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		goto reap;
	}

	do {
		ssize_t ret;

		ret = write(fd, &val, sizeof(val));
		if (ret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno) {
				pr_fail_dbg(name, "write");
				break;
			}
			continue;
		}
		val++;
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	ret = EXIT_SUCCESS;
reap:
	for (i = 0; i < opt_fifo_readers; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)waitpid(pids[i], &status, 0);
		}
	}
tidy:
	(void)unlink(fifoname);
	(void)stress_temp_dir_rm(name, pid, instance);

	return ret;
}
