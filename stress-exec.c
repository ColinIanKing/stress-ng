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
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

#if defined(STRESS_EXEC)

static uint64_t opt_exec_max = DEFAULT_EXECS;
static bool set_exec_max = false;

/*
 *  stress_set_exec_max()
 *	set maximum number of forks allowed
 */
void stress_set_exec_max(const char *optarg)
{
	set_exec_max = true;
	opt_exec_max = get_uint64_byte(optarg);
	check_range("exec-max", opt_exec_max,
		MIN_EXECS, MAX_EXECS);
}

/*
 *  stress_exec()
 *	stress by forking and exec'ing
 */
int stress_exec(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_FORKS];
	char path[PATH_MAX + 1];
	ssize_t len;
	uint64_t exec_fails = 0, exec_calls = 0;
	char *argv_new[] = { NULL, "--exec-exit", NULL };
	char *env_new[] = { NULL };

	(void)instance;

	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  executable as root.
	 */
	if (geteuid() == 0) {
		pr_inf(stderr, "%s: running as root, won't run test.\n", name);
		return EXIT_FAILURE;
        }

	/*
	 *  Determine our own self as the executable, e.g. run stress-ng
	 */
	len = readlink("/proc/self/exe", path, sizeof(path));
	if (len < 0 || len > PATH_MAX) {
		pr_fail(stderr, "%s: readlink on /proc/self/exe failed\n", name);
		return EXIT_FAILURE;
	}
	path[len] = '\0';
	argv_new[0] = path;

	do {
		unsigned int i;

		memset(pids, 0, sizeof(pids));

		for (i = 0; i < opt_exec_max; i++) {
			pids[i] = fork();

			if (pids[i] == 0) {
				int ret, fd_out, fd_in;

				setpgid(0, pgrp);
				stress_parent_died_alarm();

				if ((fd_out = open("/dev/null", O_WRONLY)) < 0) {
					pr_fail(stderr, "%s: child open on /dev/null failed\n", name);
					_exit(EXIT_FAILURE);
				}
				if ((fd_in = open("/dev/zero", O_RDONLY)) < 0) {
					pr_fail(stderr, "%s: child open on /dev/zero failed\n", name);
					(void)close(fd_out);
					_exit(EXIT_FAILURE);
				}
				dup2(fd_out, STDOUT_FILENO);
				dup2(fd_out, STDERR_FILENO);
				dup2(fd_in, STDIN_FILENO);
				(void)close(fd_out);
				(void)close(fd_in);

				ret = execve(path, argv_new, env_new);

				/* Child, immediately exit */
				_exit(ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
			}
			if (pids[i] > -1)
				setpgid(pids[i], pgrp);
			if (!opt_do_run)
				break;
		}
		for (i = 0; i < opt_exec_max; i++) {
			if (pids[i] > 0) {
				int status;
				/* Parent, wait for child */
				(void)waitpid(pids[i], &status, 0);
				exec_calls++;
				(*counter)++;
				if (WEXITSTATUS(status) != EXIT_SUCCESS)
					exec_fails++;
			}
		}

		for (i = 0; i < opt_exec_max; i++) {
			if ((pids[i] < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "%s: fork failed\n", name);
			}
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if ((exec_fails > 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail(stderr, "%s: %" PRIu64 " execs failed (%.2f%%)\n",
			name, exec_fails,
			(double)exec_fails * 100.0 / (double)(exec_calls));
	}

	return EXIT_SUCCESS;
}

#endif
