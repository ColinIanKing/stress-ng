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

static uint64_t opt_fork_max = DEFAULT_FORKS;
static bool set_fork_max = false;
static uint64_t opt_vfork_max = DEFAULT_VFORKS;
static bool set_vfork_max = false;

/*
 *  stress_set_fork_max()
 *	set maximum number of forks allowed
 */
void stress_set_fork_max(const char *optarg)
{
	set_fork_max = true;
	opt_fork_max = get_uint64_byte(optarg);
	check_range("fork-max", opt_fork_max,
		MIN_FORKS, MAX_FORKS);
}

/*
 *  stress_set_vfork_max()
 *	set maximum number of vforks allowed
 */
void stress_set_vfork_max(const char *optarg)
{
	set_vfork_max = true;
	opt_vfork_max = get_uint64_byte(optarg);
	check_range("vfork-max", opt_vfork_max,
		MIN_VFORKS, MAX_VFORKS);
}

/*
 *  stress_fork_fn()
 *	stress by forking and exiting using
 *	fork function fork_fn (fork or vfork)
 */
static int stress_fork_fn(
	const args_t *args,
	pid_t (*fork_fn)(void),
	const uint64_t fork_max)
{
	pid_t pids[MAX_FORKS];

	do {
		unsigned int i;

		memset(pids, 0, sizeof(pids));

		for (i = 0; i < fork_max; i++) {
			pid_t pid = fork_fn();

			if (pid == 0) {
				/* Child, immediately exit */
				_exit(0);
			}
			if (pid > -1)
				(void)setpgid(pids[i], g_pgrp);
			pids[i] = pid;
			if (!g_keep_stressing_flag)
				break;
		}
		for (i = 0; i < fork_max; i++) {
			if (pids[i] > 0) {
				int status;
				/* Parent, wait for child */
				(void)waitpid(pids[i], &status, 0);
				inc_counter(args);
			}
		}

		for (i = 0; i < fork_max; i++) {
			if ((pids[i] < 0) && (g_opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail("%s: fork failed\n", args->name);
			}
		}
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

/*
 *  stress_fork()
 *	stress by forking and exiting
 */
int stress_fork(const args_t *args)
{
	if (!set_fork_max) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fork_max = MAX_FORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fork_max = MIN_FORKS;
	}

	return stress_fork_fn(args, fork, opt_fork_max);
}


/*
 *  stress_vfork()
 *	stress by vforking and exiting
 */
int stress_vfork(const args_t *args)
{
	if (!set_vfork_max) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_vfork_max = MAX_VFORKS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_vfork_max = MIN_VFORKS;
	}

	return stress_fork_fn(args, vfork, opt_vfork_max);
}
