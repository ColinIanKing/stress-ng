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

#if defined(STRESS_LEASE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <setjmp.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#if defined(_POSIX_PRIORITY_SCHEDULING)
#include <sched.h>
#endif

static uint64_t lease_sigio;
static uint64_t opt_lease_breakers = DEFAULT_LEASE_BREAKERS;
static bool set_lease_breakers = false;

void stress_set_lease_breakers(const char *optarg)
{
	set_lease_breakers = true;
	opt_lease_breakers = get_uint64(optarg);
	check_range("lease-breakers", opt_lease_breakers,
		MIN_LEASE_BREAKERS, MAX_LEASE_BREAKERS);
}

/*
 *  stress_lease_handler()
 *	lease signal handler
 */
static void MLOCKED stress_lease_handler(int dummy)
{
	(void)dummy;

	lease_sigio++;
}

/*
 *  stress_lease_spawn()
 *	spawn a process
 */
static pid_t stress_lease_spawn(
	const char *filename,
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	pid_t pid;

	if (!set_lease_breakers) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_lease_breakers = MAX_LEASE_BREAKERS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_lease_breakers = MIN_LEASE_BREAKERS;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
		return -1;
	}
	if (pid == 0) {
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		do {
			int fd;

			errno = 0;
			fd = open(filename, O_NONBLOCK | O_WRONLY, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				if (errno != EWOULDBLOCK && errno != EACCES) {
					pr_dbg(stderr, "%s: open failed (child): errno=%d: (%s)\n",
						name, errno, strerror(errno));
				}
				continue;
			}
			(void)close(fd);
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		exit(EXIT_SUCCESS);
	}
	setpgid(pid, pgrp);
	return pid;
}

/*
 *  stress_lease
 *	stress by fcntl lease activity
 */
int stress_lease(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char filename[PATH_MAX];
	int ret, fd, status;
	pid_t l_pids[MAX_LEASE_BREAKERS];
	pid_t pid = getpid();
	uint64_t i;

	memset(l_pids, 0, sizeof(l_pids));

	if (stress_sighandler(name, SIGIO, stress_lease_handler, NULL) < 0)
		return EXIT_FAILURE;

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename(filename, PATH_MAX,
		name, pid, instance, mwc32());

	fd = creat(filename, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_err(stderr, "%s: creat failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		(void)stress_temp_dir_rm(name, pid, instance);
		return ret;
	}
	(void)close(fd);

	for (i = 0; i < opt_lease_breakers; i++) {
		l_pids[i] = stress_lease_spawn(filename, name, max_ops, counter);
		if (l_pids[i] < 0) {
			pr_err(stderr, "%s: failed to start all the lease breaker processes\n", name);
			goto reap;
		}
	}

	do {
		fd = open(filename, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			ret = exit_status(errno);
			pr_err(stderr, "%s: open failed (parent): errno=%d: (%s)\n",
				name, errno, strerror(errno));
			goto reap;
		}
		while (fcntl(fd, F_SETLEASE, F_WRLCK) < 0) {
			if (!opt_do_run) {
				(void)close(fd);
				goto reap;
			}
		}
		(*counter)++;
#if defined(_POSIX_PRIORITY_SCHEDULING)
		(void)sched_yield();
#endif
		if (fcntl(fd, F_SETLEASE, F_UNLCK) < 0) {
			pr_err(stderr, "%s: fcntl failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			(void)close(fd);
			break;
		}
		(void)close(fd);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	ret = EXIT_SUCCESS;

reap:
	for (i = 0; i < opt_lease_breakers; i++) {
		if (l_pids[i]) {
			(void)kill(l_pids[i], SIGKILL);
			(void)waitpid(l_pids[i], &status, 0);
		}
	}

	(void)unlink(filename);
	(void)stress_temp_dir_rm(name, pid, instance);

	pr_dbg(stderr, "%s: %" PRIu64 " lease sigio interrupts caught\n", name, lease_sigio);

	return ret;
}

#endif
