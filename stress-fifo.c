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
#include <sys/select.h>

static uint64_t opt_fifo_readers = DEFAULT_FIFO_READERS;
static bool set_fifo_readers = false;
static const uint64_t wrap_mask = 0xffff000000000000ULL;

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
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		func(name, fifoname);
		exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_fifo_readers()
 *	read fifo
 */
static void stress_fifo_reader(const char *name, const char *fifoname)
{
	int fd;
	uint64_t val, lastval = 0;

	fd = open(fifoname, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		pr_err("%s: fifo read open failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return;
	}
	while (g_keep_stressing_flag) {
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
			pr_err("%s: select failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		} else if (ret == 0) {
			pr_err("%s: read timeout!\n", name);
			break;
		}
		sz = read(fd, &val, sizeof(val));
		if (sz < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_err("%s: fifo read failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		if (sz == 0)
			break;
		if (sz != sizeof(val)) {
			pr_err("%s: fifo read did not get uint64\n",
				name);
			break;
		}
		if ((val < lastval) &&
		    ((~val & wrap_mask) && (lastval & wrap_mask))) {
			pr_err("%s: fifo read did not get "
				"expected value\n", name);
			break;
		}
		lastval = val;
	}
	(void)close(fd);
}


/*
 *  stress_fifo
 *	stress by heavy fifo I/O
 */
int stress_fifo(const args_t *args)
{
	pid_t pids[MAX_FIFO_READERS];
	int fd;
	char fifoname[PATH_MAX];
	uint64_t i, val = 0ULL;
	int rc = EXIT_FAILURE;

	if (!set_fifo_readers) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fifo_readers = MAX_FIFO_READERS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fifo_readers = MIN_FIFO_READERS;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return exit_status(-rc);

	(void)stress_temp_filename_args(args,
		fifoname, sizeof(fifoname), mwc32());
	(void)umask(0077);

	if (mkfifo(fifoname, S_IRUSR | S_IWUSR) < 0) {
		rc = exit_status(errno);
		pr_err("%s: mkfifo failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_fifo_readers; i++) {
		pids[i] = fifo_spawn(stress_fifo_reader, args->name, fifoname);
		if (pids[i] < 0)
			goto reap;
		if (!g_keep_stressing_flag) {
			rc = EXIT_SUCCESS;
			goto reap;
		}
	}

	fd = open(fifoname, O_WRONLY);
	if (fd < 0) {
		if (errno == EINTR) {
			rc = 0;
		} else {
			rc = exit_status(fd);
			pr_err("%s: fifo write open failed: "
				"errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		}
		goto reap;
	}

	do {
		ssize_t ret;

		ret = write(fd, &val, sizeof(val));
		if (ret <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			if (errno) {
				pr_fail_dbg("write");
				break;
			}
			continue;
		}
		val++;
		val &= ~wrap_mask;
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);
	rc = EXIT_SUCCESS;
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
	(void)stress_temp_dir_rm_args(args);

	return rc;
}
