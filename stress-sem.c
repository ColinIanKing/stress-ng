/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <semaphore.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

static uint64_t opt_sem_procs = DEFAULT_SEMAPHORE_PROCS;

void stress_set_sem_procs(const char *optarg)
{
	opt_sem_procs = get_uint64_byte(optarg);
	check_range("sem-procs", opt_sem_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
}



/*
 *  sem_thrash()
 *	exercise the semaphore
 */
static void sem_thrash(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	do {
		int i;

		for (i = 0; i < 1000; i++) {
			if (sem_wait(&shared->sem) < 0) {
				pr_failed_dbg(name, "sem_wait");
				break;
			}
			(*counter)++;
			sem_post(&shared->sem);
			if (!opt_do_run)
				break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  sem_spawn()
 *	spawn a process
 */
static int sem_spawn(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		return -1;
	}
	if (pid == 0) {
		sem_thrash(name, max_ops, counter);
		exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  stress_sem()
 *	stress system by sem ops
 */
int stress_semaphore(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_SEMAPHORE_PROCS];
	uint64_t i;

	(void)instance;

	if (!(opt_flags & OPT_FLAGS_SEM_INIT)) {
		pr_err(stderr, "%s: aborting, semaphore not initialised\n", name);
		return EXIT_FAILURE;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_sem_procs; i++) {
		pids[i] = sem_spawn(name, max_ops, counter);
		if (pids[i] < 0)
			goto reap;
	}
	sem_thrash(name, max_ops, counter);

reap:
	for (i = 0; i < opt_sem_procs; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
                        waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}
