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

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)
#include <semaphore.h>
#endif

static uint64_t opt_semaphore_posix_procs = DEFAULT_SEMAPHORE_PROCS;
static bool set_semaphore_posix_procs = false;

void stress_set_semaphore_posix_procs(const char *optarg)
{
	set_semaphore_posix_procs = true;
	opt_semaphore_posix_procs = get_uint64_byte(optarg);
	check_range("sem-procs", opt_semaphore_posix_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
}

#if defined(HAVE_LIB_PTHREAD) && defined(__linux__)

/*
 *  stress_semaphore_posix_init()
 *	initialize a POSIX semaphore
 */
void stress_semaphore_posix_init(void)
{
	/* create a mutex */
	if (sem_init(&g_shared->sem_posix.sem, 1, 1) >= 0) {
		g_shared->sem_posix.init = true;
		return;
	}

	if (g_opt_sequential) {
		pr_inf("semaphore init (POSIX) failed: errno=%d: "
			"(%s), skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_err("semaphore init (POSIX) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
	}
}

/*
 *  stress_semaphore_posix_destroy()
 *	destroy a POSIX semaphore
 */
void stress_semaphore_posix_destroy(void)
{
	if (g_shared->sem_posix.init) {
		if (sem_destroy(&g_shared->sem_posix.sem) < 0) {
			pr_err("semaphore destroy failed: errno=%d (%s)\n",
				errno, strerror(errno));
		}
	}
}

/*
 *  semaphore_posix_thrash()
 *	exercise the semaphore
 */
static void semaphore_posix_thrash(const args_t *args)
{
	do {
		int i;
		struct timespec timeout;

		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
			pr_fail_dbg("clock_gettime");
			return;
		}
		timeout.tv_sec++;

		for (i = 0; i < 1000; i++) {
			if (sem_timedwait(&g_shared->sem_posix.sem, &timeout) < 0) {
				if (errno == ETIMEDOUT)
					goto timed_out;
				if (errno != EINTR)
					pr_fail_dbg("sem_wait");
				break;
			}
			inc_counter(args);
			if (sem_post(&g_shared->sem_posix.sem) < 0) {
				pr_fail_dbg("sem_post");
				break;
			}
timed_out:
			if (!g_keep_stressing_flag)
				break;
		}
	} while (keep_stressing());
}

/*
 *  semaphore_posix_spawn()
 *	spawn a process
 */
static pid_t semaphore_posix_spawn(const args_t *args)
{
	pid_t pid;

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

		semaphore_posix_thrash(args);
		exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_sem()
 *	stress system by POSIX sem ops
 */
int stress_sem(const args_t *args)
{
	pid_t pids[MAX_SEMAPHORE_PROCS];
	uint64_t i;

	if (!set_semaphore_posix_procs) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_semaphore_posix_procs = MAX_SEMAPHORE_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_semaphore_posix_procs = MIN_SEMAPHORE_PROCS;
	}

	if (!g_shared->sem_posix.init) {
		pr_err("%s: aborting, semaphore not initialised\n", args->name);
		return EXIT_FAILURE;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_semaphore_posix_procs; i++) {
		pids[i] = semaphore_posix_spawn(args);
		if (!g_keep_stressing_flag || pids[i] < 0)
			goto reap;
	}

	/* Wait for termination */
	while (keep_stressing())
		(void)shim_usleep(100000);
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
#else
void stress_semaphore_posix_init(void)
{
}

void stress_semaphore_posix_destroy(void)
{
}

int stress_sem(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
