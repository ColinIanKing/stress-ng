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

#if defined(STRESS_SEMAPHORE_SYSV)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/sem.h>

static uint64_t opt_semaphore_sysv_procs = DEFAULT_SEMAPHORE_PROCS;
static bool set_semaphore_sysv_procs = false;

typedef union _semun {
	int              val;	/* Value for SETVAL */
	struct semid_ds *buf;	/* Buffer for IPC_STAT, IPC_SET */
	unsigned short  *array;	/* Array for GETALL, SETALL */
	struct seminfo  *__buf;	/* Buffer for IPC_INFO (Linux-specific) */
} semun_t;

void stress_set_semaphore_sysv_procs(const char *optarg)
{
	set_semaphore_sysv_procs = true;
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
		shared->sem_sysv.key_id = (key_t)mwc16();
		shared->sem_sysv.sem_id =
			semget(shared->sem_sysv.key_id, 1,
				IPC_CREAT | S_IRUSR | S_IWUSR);
		if (shared->sem_sysv.sem_id >= 0)
			break;

		count++;
	}

	if (shared->sem_sysv.sem_id >= 0) {
		semun_t arg;

		arg.val = 1;
		if (semctl(shared->sem_sysv.sem_id, 0, SETVAL, arg) == 0) {
			shared->sem_sysv.init = true;
			return;
		}
		/* Clean up */
		(void)semctl(shared->sem_sysv.sem_id, 0, IPC_RMID);
	}

	if (opt_sequential) {
		pr_inf(stderr, "semaphore init (System V) failed: errno=%d: "
			"(%s), skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_err(stderr, "semaphore init (System V) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
		exit(EXIT_FAILURE);
	}
}

/*
 *  stress_semaphore_sysv_destory()
 *	destroy a System V semaphore
 */
void stress_semaphore_sysv_destroy(void)
{
	if (shared->sem_sysv.init)
		(void)semctl(shared->sem_sysv.sem_id, 0, IPC_RMID);
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
	const int sem_id = shared->sem_sysv.sem_id;

	do {
		int i;
		struct timespec timeout;

		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
			pr_fail_dbg(name, "clock_gettime");
			return;
		}
		timeout.tv_sec++;

		for (i = 0; i < 1000; i++) {
			struct sembuf semwait, semsignal;

			semwait.sem_num = 0;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;

			semsignal.sem_num = 0;
			semsignal.sem_op = 1;
			semsignal.sem_flg = SEM_UNDO;

			if (semtimedop(sem_id, &semwait, 1, &timeout) < 0) {
				if (errno == EAGAIN) {
					pr_inf(stderr, "Semaphore timed out: errno=%d (%s)\n",
						errno, strerror(errno));
					goto timed_out;
				}
				if (errno != EINTR)
					pr_fail_dbg(name, "semop wait");
				break;
			}
			(*counter)++;
			if (semop(sem_id, &semsignal, 1) < 0) {
				if (errno != EINTR)
					pr_fail_dbg(name, "semop signal");
				break;
			}
timed_out:
			if (!opt_do_run)
				break;
		}
#if defined(IPC_STAT)
		{
			struct semid_ds ds;
			semun_t s;

			s.buf = &ds;
			if (semctl(sem_id, 0, IPC_STAT, &s) < 0)
				pr_fail_dbg(name, "semctl IPC_STAT");
		}
#endif
#if defined(SEM_STAT)
		{
			struct semid_ds ds;
			semun_t s;

			s.buf = &ds;
			if (semctl(sem_id, 0, SEM_STAT, &s) < 0)
				pr_fail_dbg(name, "semctl SEM_STAT");
		}
#endif
#if defined(IPC_INFO)
		{
			struct seminfo si;
			semun_t s;

			s.__buf = &si;
			if (semctl(sem_id, 0, IPC_INFO, &s) < 0)
				pr_fail_dbg(name, "semctl IPC_INFO");
		}
#endif
#if defined(SEM_INFO)
		{
			struct seminfo si;
			semun_t s;

			s.__buf = &si;
			if (semctl(sem_id, 0, SEM_INFO, &s) < 0)
				pr_fail_dbg(name, "semctl SEM_INFO");
		}
#endif
#if defined(GETVAL)
		if (semctl(sem_id, 0, GETVAL) < 0)
			pr_fail_dbg(name, "semctl GETVAL");
#endif
#if defined(GETPID)
		if (semctl(sem_id, 0, GETPID) < 0)
			pr_fail_dbg(name, "semctl GETPID");
#endif
#if defined(GETNCNT)
		if (semctl(sem_id, 0, GETNCNT) < 0)
			pr_fail_dbg(name, "semctl GETNCNT");
#endif
#if defined(GEZCNT)
		if (semctl(sem_id, 0, GETZCNT) < 0)
			pr_fail_dbg(name, "semctl GETZCNT");
#endif
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  semaphore_sysv_spawn()
 *	spawn a process
 */
static pid_t semaphore_sysv_spawn(
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

		semaphore_sysv_thrash(name, max_ops, counter);
		exit(EXIT_SUCCESS);
	}
	setpgid(pid, pgrp);
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

	if (!set_semaphore_sysv_procs) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_semaphore_sysv_procs = MAX_SEMAPHORE_PROCS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_semaphore_sysv_procs = MIN_SEMAPHORE_PROCS;
	}

	if (!shared->sem_sysv.init) {
		pr_err(stderr, "%s: aborting, semaphore not initialised\n", name);
		return EXIT_FAILURE;
	}

	memset(pids, 0, sizeof(pids));
	for (i = 0; i < opt_semaphore_sysv_procs; i++) {
		pids[i] = semaphore_sysv_spawn(name, max_ops, counter);
		if (!opt_do_run || pids[i] < 0)
			goto reap;
	}
	/* Wait for termination */
	while (opt_do_run && (!max_ops || *counter < max_ops))
		usleep(100000);
reap:
	for (i = 0; i < opt_semaphore_sysv_procs; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGKILL);
	}
	for (i = 0; i < opt_semaphore_sysv_procs; i++) {
		if (pids[i] > 0) {
			int status;

                        (void)waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

#endif
