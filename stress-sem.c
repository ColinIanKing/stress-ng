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

#if defined(STRESS_SEMAPHORE_POSIX)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <semaphore.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>

static uint64_t opt_semaphore_posix_procs = DEFAULT_SEMAPHORE_PROCS;
static bool set_semaphore_posix_procs = false;

void stress_set_semaphore_posix_procs(const char *optarg)
{
	set_semaphore_posix_procs = true;
	opt_semaphore_posix_procs = get_uint64_byte(optarg);
	check_range("sem-procs", opt_semaphore_posix_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
}

/*
 *  stress_semaphore_posix_init()
 *	initialize a POSIX semaphore
 */
void stress_semaphore_posix_init(void)
{
	/* create a mutex */
	if (sem_init(&shared->sem_posix.sem, 1, 1) >= 0) {
		shared->sem_posix.init = true;
		return;
	}

	if (opt_sequential) {
		pr_inf(stderr, "semaphore init (POSIX) failed: errno=%d: "
			"(%s), skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_err(stderr, "semaphore init (POSIX) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
	}
}

/*
 *  stress_semaphore_posix_destroy()
 *	destroy a POSIX semaphore
 */
void stress_semaphore_posix_destroy(void)
{
        if (shared->sem_posix.init) {
		if (sem_destroy(&shared->sem_posix.sem) < 0) {
			pr_err(stderr, "semaphore destroy failed: errno=%d (%s)\n",
				errno, strerror(errno));
		}
	}
}

/*
 *  semaphore_posix_thrash()
 *	exercise the semaphore
 */
static void semaphore_posix_thrash(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	do {
		int i;
		struct timespec timeout;

		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
			pr_fail_dbg(name, "clock_gettime");
			return;
		}
		timeout.tv_sec++;

		for (i = 0; i < 1000; i++) {
			if (sem_timedwait(&shared->sem_posix.sem, &timeout) < 0) {
				if (errno == ETIMEDOUT)
					goto timed_out;
				if (errno != EINTR)
					pr_fail_dbg(name, "sem_wait");
				break;
			}
			(*counter)++;
			if (sem_post(&shared->sem_posix.sem) < 0) {
				pr_fail_dbg(name, "sem_post");
				break;
			}
timed_out:
			if (!opt_do_run)
				break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  semaphore_posix_spawn()
 *	spawn a process
 */
static pid_t semaphore_posix_spawn(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	pid_t pid;

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

		semaphore_posix_thrash(name, max_ops, counter);
		exit(EXIT_SUCCESS);
	}
	setpgid(pid, pgrp);
	return pid;
}

/*
 *  stress_sem()
 *	stress system by POSIX sem ops
 */
int stress_sem(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_SEMAPHORE_PROCS];
	uint64_t i;

	(void)instance;

	if (!set_semaphore_posix_procs) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_semaphore_posix_procs = MAX_SEMAPHORE_PROCS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_semaphore_posix_procs = MIN_SEMAPHORE_PROCS;
	}

	if (!shared->sem_posix.init) {
		pr_err(stderr, "%s: aborting, semaphore not initialised\n", name);
		return EXIT_FAILURE;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_semaphore_posix_procs; i++) {
		pids[i] = semaphore_posix_spawn(name, max_ops, counter);
		if (!opt_do_run || pids[i] < 0)
			goto reap;
	}

	/* Wait for termination */
	while (opt_do_run && (!max_ops || *counter < max_ops))
		usleep(100000);
reap:
	for (i = 0; i < opt_semaphore_posix_procs; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGKILL);
	}
	for (i = 0; i < opt_semaphore_posix_procs; i++) {
		if (pids[i] > 0) {
			int status;

                        (void)waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

#endif
