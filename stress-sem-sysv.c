/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#if !defined(__gnu_hurd__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>

#include "stress-ng.h"

static uint64_t opt_semaphore_sysv_procs = DEFAULT_SEMAPHORE_PROCS;

void stress_set_semaphore_sysv_procs(const char *optarg)
{
	opt_semaphore_sysv_procs = get_uint64_byte(optarg);
	check_range("sem-procs", opt_semaphore_sysv_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
}

/*
 *  stress_semaphore_sysv_init()
 *	initialise a System V semaphore
 */
void stress_semaphore_sysv_init(void)
{
	int count = 0;

	while (count < 100) {
		shared->sem_sysv_key_id = (key_t)(mwc() & 0xffff);
		shared->sem_sysv_id = semget(shared->sem_sysv_key_id, 1, IPC_CREAT | S_IRUSR | S_IWUSR);
		if (shared->sem_sysv_id >= 0)
			break;

		count++;
	}

	if (shared->sem_sysv_id >= 0) {
		unsigned short semval = 1;
		if (semctl(shared->sem_sysv_id, 0, SETVAL, semval) == 0) {
			shared->sem_sysv_init = true;
			return;
		}
		/* Clean up */
		(void)semctl(shared->sem_sysv_id, 0, IPC_RMID);
	}

	if (opt_sequential) {
		pr_inf(stderr, "Semaphore init failed: errno=%d: (%s), "
			"skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_err(stderr, "Semaphore init failed: errno=%d: (%s)\n",
			errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/*
 *  stress_semaphore_sysv_destory()
 *	destroy a System V semaphore
 */
void stress_semaphore_sysv_destroy(void)
{
	if (shared->sem_sysv_init)
		(void)semctl(shared->sem_sysv_id, 0, IPC_RMID);
}

/*
 *  semaphore_sysv_thrash()
 *	exercise the semaphore
 */
static void semaphore_sysv_thrash(
	const char *name,
	const uint64_t max_ops,
	uint64_t *counter)
{
	do {
		int i;

		for (i = 0; i < 1000; i++) {
			struct sembuf semwait, semsignal;

			semwait.sem_num = 0;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;

			semsignal.sem_num = 0;
			semsignal.sem_op = 1;
			semsignal.sem_flg = SEM_UNDO;

			if (semop(shared->sem_sysv_id, &semwait, 1) < 0) {
				if (errno != EINTR)
					pr_failed_dbg(name, "semop wait");
				break;
			}
			(*counter)++;
			if (semop(shared->sem_sysv_id, &semsignal, 1) < 0) {
				if (errno != EINTR)
					pr_failed_dbg(name, "semop signal");
				break;
			}
			if (!opt_do_run)
				break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  semaphore_sysv_spawn()
 *	spawn a process
 */
static int semaphore_sysv_spawn(
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
		semaphore_sysv_thrash(name, max_ops, counter);
		exit(EXIT_SUCCESS);
	}
	return pid;
}

/*
 *  stress_sem_sysv()
 *	stress system by sem ops
 */
int stress_sem_sysv(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_SEMAPHORE_PROCS];
	uint64_t i;

	(void)instance;

	if (!shared->sem_sysv_init) {
		pr_err(stderr, "%s: aborting, semaphore not initialised\n", name);
		return EXIT_FAILURE;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_semaphore_sysv_procs; i++) {
		pids[i] = semaphore_sysv_spawn(name, max_ops, counter);
		if (pids[i] < 0)
			goto reap;
	}
	semaphore_sysv_thrash(name, max_ops, counter);
reap:
	for (i = 0; i < opt_semaphore_sysv_procs; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
                        waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

#endif
