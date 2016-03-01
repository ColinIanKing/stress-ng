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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>

#if defined(STRESS_SPAWN)

/*
 *  stress_spawn()
 *	stress by forking and spawn'ing
 */
int stress_spawn(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char path[PATH_MAX + 1];
	ssize_t len;
	uint64_t spawn_fails = 0, spawn_calls = 0;
	static char *argv_new[] = { NULL, "--exec-exit", NULL };
	static char *env_new[] = { NULL };

	(void)instance;

	/*
	 *  Don't want to run this when running as root as
	 *  this could allow somebody to try and run another
	 *  spawnutable as root.
	 */
	if (geteuid() == 0) {
		pr_inf(stderr, "%s: running as root, won't run test.\n", name);
		return EXIT_FAILURE;
        }

	/*
	 *  Determine our own self as the spawnutable, e.g. run stress-ng
	 */
	len = readlink("/proc/self/exe", path, sizeof(path));
	if (len < 0 || len > PATH_MAX) {
		pr_fail(stderr, "%s: readlink on /proc/self/exe failed\n", name);
		return EXIT_FAILURE;
	}
	path[len] = '\0';
	argv_new[0] = path;

	do {
		int ret;
		pid_t pid;

		spawn_calls++;
		ret = posix_spawn(&pid, path, NULL, NULL, argv_new, env_new);
		if (ret < 0) {
			pr_fail_err(name, "posix_spawn()");
			spawn_fails++;
		} else {
			int status;
			/* Parent, wait for child */

			(void)waitpid(pid, &status, 0);
			(*counter)++;
			if (WEXITSTATUS(status) != EXIT_SUCCESS)
				spawn_fails++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if ((spawn_fails > 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
		pr_fail(stderr, "%s: %" PRIu64 " spawns failed (%.2f%%)\n",
			name, spawn_fails,
			(double)spawn_fails * 100.0 / (double)(spawn_calls));
	}

	return EXIT_SUCCESS;
}

#endif
