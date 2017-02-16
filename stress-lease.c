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

#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)

static uint64_t lease_sigio;
#endif

static uint64_t opt_lease_breakers = DEFAULT_LEASE_BREAKERS;
static bool set_lease_breakers = false;

void stress_set_lease_breakers(const char *optarg)
{
	set_lease_breakers = true;
	opt_lease_breakers = get_uint64(optarg);
	check_range("lease-breakers", opt_lease_breakers,
		MIN_LEASE_BREAKERS, MAX_LEASE_BREAKERS);
}

#if defined(F_SETLEASE) && defined(F_WRLCK) && defined(F_UNLCK)

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
	const args_t *args,
	const char *filename)
{
	pid_t pid;

	if (!set_lease_breakers) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_lease_breakers = MAX_LEASE_BREAKERS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_lease_breakers = MIN_LEASE_BREAKERS;
	}

again:
	pid = fork();
	if (pid < 0) {
		if (g_keep_stressing_flag && (errno == EAGAIN))
			goto again;
		return -1;
	}
	if (pid == 0) {
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		do {
			int fd;

			errno = 0;
			fd = open(filename, O_NONBLOCK | O_WRONLY, S_IRUSR | S_IWUSR);
			if (fd < 0) {
				if (errno != EWOULDBLOCK && errno != EACCES) {
					pr_dbg("%s: open failed (child): errno=%d: (%s)\n",
						args->name, errno, strerror(errno));
				}
				continue;
			}
			(void)close(fd);
		} while (keep_stressing());
		exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_lease
 *	stress by fcntl lease activity
 */
int stress_lease(const args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd, status;
	pid_t l_pids[MAX_LEASE_BREAKERS];
	uint64_t i;

	memset(l_pids, 0, sizeof(l_pids));

	if (stress_sighandler(args->name, SIGIO, stress_lease_handler, NULL) < 0)
		return EXIT_FAILURE;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	fd = creat(filename, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		ret = exit_status(errno);
		pr_err("%s: creat failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)close(fd);

	for (i = 0; i < opt_lease_breakers; i++) {
		l_pids[i] = stress_lease_spawn(args, filename);
		if (l_pids[i] < 0) {
			pr_err("%s: failed to start all the lease breaker processes\n", args->name);
			goto reap;
		}
	}

	do {
		fd = open(filename, O_WRONLY | O_APPEND, S_IRUSR | S_IWUSR);
		if (fd < 0) {
			ret = exit_status(errno);
			pr_err("%s: open failed (parent): errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			goto reap;
		}
		while (fcntl(fd, F_SETLEASE, F_WRLCK) < 0) {
			if (!g_keep_stressing_flag) {
				(void)close(fd);
				goto reap;
			}
		}
		inc_counter(args);
		(void)shim_sched_yield();
		if (fcntl(fd, F_SETLEASE, F_UNLCK) < 0) {
			pr_err("%s: fcntl failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			(void)close(fd);
			break;
		}
		(void)close(fd);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;

reap:
	for (i = 0; i < opt_lease_breakers; i++) {
		if (l_pids[i]) {
			(void)kill(l_pids[i], SIGKILL);
			(void)waitpid(l_pids[i], &status, 0);
		}
	}

	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	pr_dbg("%s: %" PRIu64 " lease sigio interrupts caught\n", args->name, lease_sigio);

	return ret;
}
#else
int stress_lease(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
