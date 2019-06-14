/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,	"sem-sysv-ops N",	"stop after N System V sem bogo operations" },
	{ NULL,	"sem-sysv-procs N",	"number of processes to start per worker" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SEM_SYSV)
typedef union _semun {
	int              val;	/* Value for SETVAL */
	struct semid_ds *buf;	/* Buffer for IPC_STAT, IPC_SET */
	unsigned short  *array;	/* Array for GETALL, SETALL */
	struct seminfo  *__buf;	/* Buffer for IPC_INFO (Linux-specific) */
} semun_t;
#endif

static int stress_set_semaphore_sysv_procs(const char *opt)
{
	uint64_t semaphore_sysv_procs;

	semaphore_sysv_procs = get_uint64(opt);
	check_range("sem-sysv-procs", semaphore_sysv_procs,
		MIN_SEMAPHORE_PROCS, MAX_SEMAPHORE_PROCS);
	return set_setting("sem-sysv-procs", TYPE_ID_UINT64, &semaphore_sysv_procs);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sem_sysv_procs,	stress_set_semaphore_sysv_procs },
	{ 0,			NULL }
};

#if defined(HAVE_SEM_SYSV)
/*
 *  stress_semaphore_sysv_init()
 *	initialise a System V semaphore
 */
static void stress_semaphore_sysv_init(void)
{
	int count = 0;

	while (count < 100) {
		g_shared->sem_sysv.key_id = (key_t)mwc16();
		g_shared->sem_sysv.sem_id =
			semget(g_shared->sem_sysv.key_id, 1,
				IPC_CREAT | S_IRUSR | S_IWUSR);
		if (g_shared->sem_sysv.sem_id >= 0)
			break;

		count++;
	}

	if (g_shared->sem_sysv.sem_id >= 0) {
		semun_t arg;

		arg.val = 1;
		if (semctl(g_shared->sem_sysv.sem_id, 0, SETVAL, arg) == 0) {
			g_shared->sem_sysv.init = true;
			return;
		}
		/* Clean up */
		(void)semctl(g_shared->sem_sysv.sem_id, 0, IPC_RMID);
	}

	if (g_opt_sequential) {
		pr_inf("semaphore init (System V) failed: errno=%d: "
			"(%s), skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_err("semaphore init (System V) failed: errno=%d: "
			"(%s)\n", errno, strerror(errno));
		_exit(EXIT_FAILURE);
	}
}

/*
 *  stress_semaphore_sysv_destory()
 *	destroy a System V semaphore
 */
static void stress_semaphore_sysv_deinit(void)
{
	if (g_shared->sem_sysv.init)
		(void)semctl(g_shared->sem_sysv.sem_id, 0, IPC_RMID);
}


#if defined(__linux__)
/*
 *  stress_semaphore_sysv_get_procinfo()
 *	exercise /proc/sysvipc/sem
 */
static void stress_semaphore_sysv_get_procinfo(bool *get_procinfo)
{
	int fd;

	fd = open("/proc/sysvipc/sem", O_RDONLY);
	if (fd < 0) {
		*get_procinfo = false;
		return;
	}
	for (;;) {
		ssize_t ret;
		char buffer[1024];

		ret = read(fd, buffer, sizeof(buffer));
		if (ret <= 0)
			break;
	}
	(void)close(fd);
}
#endif

/*
 *  stress_semaphore_sysv_thrash()
 *	exercise the semaphore
 */
static void stress_semaphore_sysv_thrash(const args_t *args)
{
	const int sem_id = g_shared->sem_sysv.sem_id;

	do {
		int i;
#if defined(__linux__)
		bool get_procinfo = true;
#endif
#if defined(HAVE_SEMTIMEDOP) &&	\
    defined(HAVE_CLOCK_GETTIME)
		struct timespec timeout;

		if (clock_gettime(CLOCK_REALTIME, &timeout) < 0) {
			pr_fail_dbg("clock_gettime");
			return;
		}
		timeout.tv_sec++;
#endif

#if defined(__linux__)
		if (get_procinfo)
			stress_semaphore_sysv_get_procinfo(&get_procinfo);
#endif

		for (i = 0; i < 1000; i++) {
			struct sembuf semwait, semsignal;

			semwait.sem_num = 0;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;

			semsignal.sem_num = 0;
			semsignal.sem_op = 1;
			semsignal.sem_flg = SEM_UNDO;

#if defined(HAVE_SEMTIMEDOP) &&	\
    defined(HAVE_CLOCK_GETTIME)
			if (semtimedop(sem_id, &semwait, 1, &timeout) < 0) {
#else
			if (semop(sem_id, &semwait, 1) < 0) {
#endif
				if (errno == EAGAIN)
					goto timed_out;
				if (errno != EINTR)
					pr_fail_dbg("semop wait");
				break;
			}
			if (semop(sem_id, &semsignal, 1) < 0) {
				if (errno != EINTR)
					pr_fail_dbg("semop signal");
				break;
			}
timed_out:
			if (!keep_stressing())
				break;
			inc_counter(args);
		}
#if defined(IPC_STAT)
		{
			struct semid_ds ds;
			semun_t s;

			s.buf = &ds;
			if (semctl(sem_id, 0, IPC_STAT, &s) < 0)
				pr_fail_dbg("semctl IPC_STAT");
		}
#endif
#if defined(SEM_STAT)
		{
			struct semid_ds ds;
			semun_t s;

			s.buf = &ds;
			if (semctl(sem_id, 0, SEM_STAT, &s) < 0)
				pr_fail_dbg("semctl SEM_STAT");
		}
#endif
#if defined(IPC_INFO) && defined(__linux__)
		{
			struct seminfo si;
			semun_t s;

			s.__buf = &si;
			if (semctl(sem_id, 0, IPC_INFO, &s) < 0)
				pr_fail_dbg("semctl IPC_INFO");
		}
#endif
#if defined(SEM_INFO) && defined(__linux__)
		{
			struct seminfo si;
			semun_t s;

			s.__buf = &si;
			if (semctl(sem_id, 0, SEM_INFO, &s) < 0)
				pr_fail_dbg("semctl SEM_INFO");
		}
#endif
#if defined(GETVAL)
		if (semctl(sem_id, 0, GETVAL) < 0)
			pr_fail_dbg("semctl GETVAL");
#endif
#if defined(GETPID)
		if (semctl(sem_id, 0, GETPID) < 0)
			pr_fail_dbg("semctl GETPID");
#endif
#if defined(GETNCNT)
		if (semctl(sem_id, 0, GETNCNT) < 0)
			pr_fail_dbg("semctl GETNCNT");
#endif
#if defined(GEZCNT)
		if (semctl(sem_id, 0, GETZCNT) < 0)
			pr_fail_dbg("semctl GETZCNT");
#endif
	} while (keep_stressing());
}

/*
 *  semaphore_sysv_spawn()
 *	spawn a process
 */
static pid_t semaphore_sysv_spawn(const args_t *args)
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

		stress_semaphore_sysv_thrash(args);
		_exit(EXIT_SUCCESS);
	}
	(void)setpgid(pid, g_pgrp);
	return pid;
}

/*
 *  stress_sem_sysv()
 *	stress system by sem ops
 */
static int stress_sem_sysv(const args_t *args)
{
	pid_t pids[MAX_SEMAPHORE_PROCS];
	uint64_t i;
	uint64_t semaphore_sysv_procs = DEFAULT_SEMAPHORE_PROCS;

	if (!get_setting("sem-sysv-procs", &semaphore_sysv_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			semaphore_sysv_procs = MAX_SEMAPHORE_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			semaphore_sysv_procs = MIN_SEMAPHORE_PROCS;
	}

	if (!g_shared->sem_sysv.init) {
		pr_err("%s: aborting, semaphore not initialised\n", args->name);
		return EXIT_FAILURE;
	}

	(void)memset(pids, 0, sizeof(pids));
	for (i = 0; i < semaphore_sysv_procs; i++) {
		pids[i] = semaphore_sysv_spawn(args);
		if (!g_keep_stressing_flag || pids[i] < 0)
			goto reap;
	}
	/* Wait for termination */
	while (keep_stressing())
		(void)shim_usleep(100000);
reap:
	for (i = 0; i < semaphore_sysv_procs; i++) {
		if (pids[i] > 0)
			(void)kill(pids[i], SIGKILL);
	}
	for (i = 0; i < semaphore_sysv_procs; i++) {
		if (pids[i] > 0) {
			int status;

			(void)shim_waitpid(pids[i], &status, 0);
		}
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_sem_sysv_info = {
	.stressor = stress_sem_sysv,
	.init = stress_semaphore_sysv_init,
	.deinit = stress_semaphore_sysv_deinit,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sem_sysv_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
