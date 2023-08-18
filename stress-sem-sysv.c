// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_SEM_SYSV)
#include <sys/sem.h>
#else
UNEXPECTED
#endif

#define MIN_SEM_SYSV_PROCS     (2)
#define MAX_SEM_SYSV_PROCS     (64)
#define DEFAULT_SEM_SYSV_PROCS (2)

#define STRESS_MAX_SEMS		(100)

static const stress_help_t help[] = {
	{ NULL,	"sem-sysv N",		"start N workers doing System V semaphore operations" },
	{ NULL,	"sem-sysv-ops N",	"stop after N System V sem bogo operations" },
	{ NULL,	"sem-sysv-procs N",	"number of processes to start per worker" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_SEM_SYSV)
typedef union stress_semun {
	int              val;	/* Value for SETVAL */
	struct semid_ds *buf;	/* Buffer for IPC_STAT, IPC_SET */		/* cppcheck-suppress unusedStructMember */
	unsigned short  *array;	/* Array for GETALL, SETALL */			/* cppcheck-suppress unusedStructMember */
	struct seminfo  *__buf;	/* Buffer for IPC_INFO (Linux-specific) */	/* cppcheck-suppress unusedStructMember */
} stress_semun_t;
#endif

static int stress_set_semaphore_sysv_procs(const char *opt)
{
	uint64_t semaphore_sysv_procs;

	semaphore_sysv_procs = stress_get_uint64(opt);
	stress_check_range("sem-sysv-procs", semaphore_sysv_procs,
		MIN_SEM_SYSV_PROCS, MAX_SEM_SYSV_PROCS);
	return stress_set_setting("sem-sysv-procs", TYPE_ID_UINT64, &semaphore_sysv_procs);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
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
	int count = 0, sem_id;

	/* Exercise invalid nsems, EINVAL */
	sem_id = semget((key_t)stress_mwc16(), -1, IPC_CREAT | S_IRUSR | S_IWUSR);
	if (sem_id != -1)
		(void)semctl(sem_id, 0, IPC_RMID);

	/* Exercise invalid nsems, EINVAL */
	sem_id = semget((key_t)stress_mwc16(), INT_MAX, IPC_CREAT | S_IRUSR | S_IWUSR);
	if (sem_id != -1)
		(void)semctl(sem_id, 0, IPC_RMID);

	/* Exercise invalid semget semflg, EINVAL */
	sem_id = semget((key_t)stress_mwc16(), 0, ~0);
	if (sem_id != -1)
		(void)semctl(sem_id, 0, IPC_RMID);

	/* Exercise without IPC_CREAT, ENOENT */
	sem_id = semget((key_t)stress_mwc16(), 1, S_IRUSR | S_IWUSR);
	if (sem_id != -1)
		(void)semctl(sem_id, 0, IPC_RMID);

	while (count < 100) {
		/* use odd key so it won't clash with even core-resource sem keys */
		g_shared->sem_sysv.key_id = (key_t)stress_mwc16() | 1;
		g_shared->sem_sysv.sem_id =
			semget(g_shared->sem_sysv.key_id, 3,
				IPC_CREAT | S_IRUSR | S_IWUSR);
		if (g_shared->sem_sysv.sem_id >= 0)
			break;

		count++;
	}

	if (g_shared->sem_sysv.sem_id >= 0) {
		stress_semun_t arg;

		arg.val = 1;
		if (semctl(g_shared->sem_sysv.sem_id, 0, SETVAL, arg) == 0) {
			g_shared->sem_sysv.init = true;
			return;
		}
		/* Clean up */
		(void)semctl(g_shared->sem_sysv.sem_id, 0, IPC_RMID);
	}

	if (g_opt_sequential) {
		pr_inf_skip("semaphore init (System V) failed: errno=%d: "
			"(%s), skipping semaphore stressor\n",
			errno, strerror(errno));
	} else {
		pr_fail("semaphore init (System V) failed: errno=%d: "
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
static int OPTIMIZE3 stress_semaphore_sysv_thrash(const stress_args_t *args)
{
	const int sem_id = g_shared->sem_sysv.sem_id;
	int rc = EXIT_SUCCESS;
#if defined(HAVE_SEMTIMEDOP)
	bool got_semtimedop = true;
#endif

	do {
		int i, ret;
#if defined(__linux__)
		bool get_procinfo = true;
#endif

#if defined(__linux__)
		if (get_procinfo)
			stress_semaphore_sysv_get_procinfo(&get_procinfo);
#endif

#if defined(HAVE_SEMTIMEDOP)
		if (got_semtimedop) {
			struct timespec timeout ALIGN64;
			struct sembuf sems[STRESS_MAX_SEMS * 3] ALIGN64;

			for (i = 0; i < STRESS_MAX_SEMS * 3; i += 3) {
				sems[i].sem_num = 1;
				sems[i].sem_op = 1;
				sems[i].sem_flg = SEM_UNDO;

				sems[i + 1].sem_num = 1;
				sems[i + 1].sem_op = 1;
				sems[i + 1].sem_flg = SEM_UNDO;

				sems[i + 2].sem_num = 1;
				sems[i + 2].sem_op = -1;
				sems[i + 2].sem_flg = SEM_UNDO;
			}
			timeout.tv_sec = 0;
			timeout.tv_nsec = 100000;
			VOID_RET(int, semtimedop(sem_id, sems, STRESS_MAX_SEMS * 3, &timeout));
		}
#else
		UNEXPECTED
#endif
		for (i = 0; i < 1000; i++) {
			struct sembuf semwait, semsignal;

			semwait.sem_num = 0;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;

			semsignal.sem_num = 0;
			semsignal.sem_op = 1;
			semsignal.sem_flg = SEM_UNDO;

#if defined(HAVE_SEMTIMEDOP)
			if (got_semtimedop) {
				struct timespec timeout ALIGN64;

				timeout.tv_sec = 1;
				timeout.tv_nsec = 0;
				ret = semtimedop(sem_id, &semwait, 1, &timeout);
				if ((ret < 0) && ((errno == ENOSYS) || (errno == EINVAL))) {
					got_semtimedop = false;
					ret = semop(sem_id, &semwait, 1);
				}
			} else {
				ret = semop(sem_id, &semwait, 1);
			}
#else
			ret = semop(sem_id, &semwait, 1);
#endif
			if (UNLIKELY(ret < 0)) {
				if (errno == EAGAIN)
					goto timed_out;
				if (errno != EINTR) {
					pr_fail("%s: semop wait failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
				break;
			}
			if (UNLIKELY(semop(sem_id, &semsignal, 1) < 0)) {
				if (errno != EINTR) {
					pr_fail("%s: semop signal failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
				}
				break;
			}
timed_out:
			stress_bogo_inc(args);
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
#if defined(IPC_STAT)
		{
			struct semid_ds ds;
			stress_semun_t s;
#if defined(GETALL)
			size_t nsems;
#endif

			(void)shim_memset(&ds, 0, sizeof(ds));

			s.buf = &ds;
			if (UNLIKELY(semctl(sem_id, 2, IPC_STAT, &s) < 0)) {
				pr_fail("%s: semctl IPC_STAT failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			} else {
#if defined(IPC_SET)
				s.buf = &ds;
				VOID_RET(int, semctl(sem_id, 2, IPC_SET, &s));
#else
				UNEXPECTED
#endif
			}
#if defined(GETALL)
			/* Avoid zero array size allocation */
			nsems = ds.sem_nsems;
			if (nsems < 64)
				nsems = 64;
			s.array = calloc(nsems, sizeof(*s.array));
			if (s.array) {
				VOID_RET(int, semctl(sem_id, 2, GETALL, s));
#if defined(SETALL) && 0
				/*
				 *  SETALL across the semaphores will clobber the state
				 *  and cause waits on semaphores because of the unlocked
				 *  state change. Currently this is disabled.
				 */
				if (ret == 0)
					ret = semctl(sem_id, 2, SETALL, s);
#endif
				free(s.array);
			}
#endif
		}
#else
		UNEXPECTED
#endif
#if defined(SEM_STAT) &&	\
    defined(__linux__)
		{
			struct semid_ds ds;
			stress_semun_t s;

			/* Exercise with a 0 index into kernel array */
			s.buf = &ds;
			VOID_RET(int, semctl(0, 0, SEM_STAT, &s));

			/* Exercise with a probably illegal index into kernel array */
			s.buf = &ds;
			VOID_RET(int, semctl(0x1fffffff, 0, SEM_STAT, &s));
		}
#endif
#if defined(SEM_STAT_ANY) &&	\
    defined(__linux__)
		{
			struct seminfo si;
			stress_semun_t s;

			/* Exercise with a 0 index into kernel array */
			s.__buf = &si;
			VOID_RET(int, semctl(0, 0, SEM_STAT_ANY, &s));

			/* Exercise with a probably illegal index into kernel array */
			s.__buf = &si;
			VOID_RET(int, semctl(0x1fffffff, 0, SEM_STAT_ANY, &s));
		}
#endif
#if defined(IPC_INFO) &&	\
    defined(__linux__)
		{
			struct seminfo si;
			stress_semun_t s;

			s.__buf = &si;
			if (UNLIKELY(semctl(sem_id, 0, IPC_INFO, &s) < 0)) {
				pr_fail("%s: semctl IPC_INFO failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}

			/*
			 *  Exercise with probably an illegal sem id, this arg is
			 *  actually ignored, so should be OK
			 */
			s.__buf = &si;
			VOID_RET(int, semctl(0x1fffffff, 0, IPC_INFO, &s));

			/*
			 *  Exercise with an illegal sem number, this arg is
			 *  also ignored, so should be OK
			 */
			s.__buf = &si;
			VOID_RET(int, semctl(sem_id, ~0, IPC_INFO, &s));
		}
#endif
#if defined(SEM_INFO) &&	\
    defined(__linux__)
		{
			struct seminfo si;
			stress_semun_t s;

			s.__buf = &si;
			if (UNLIKELY(semctl(sem_id, 0, SEM_INFO, &s) < 0)) {
				pr_fail("%s: semctl SEM_INFO failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				rc = EXIT_FAILURE;
			}

			/* Exercise with probably an illegal sem id */
			s.__buf = &si;
			VOID_RET(int, semctl(0x1fffffff, 0, IPC_INFO, &s));

			/* Exercise with an illegal sem number */
			s.__buf = &si;
			VOID_RET(int, semctl(sem_id, ~0, IPC_INFO, &s));
		}
#endif
		if (UNLIKELY(!stress_continue(args)))
			break;
#if defined(GETVAL)
		VOID_RET(int, semctl(sem_id, 0, GETVAL, 0));

		/* Exercise with probably an illegal sem id */
		VOID_RET(int, semctl(0x1fffffff, 0, GETVAL, 0));

		/* Exercise with an illegal sem number */
		VOID_RET(int, semctl(sem_id, ~0, GETVAL, 0));
#else
		UNEXPECTED
#endif
#if defined(GETPID)
		VOID_RET(int, semctl(sem_id, 0, GETPID, 0));

		/* Exercise with probably an illegal sem id */
		VOID_RET(int, semctl(0x1fffffff, 0, GETPID, 0));

		/* Exercise with an illegal sem number */
		VOID_RET(int, semctl(sem_id, ~0, GETPID, 0));
#else
		UNEXPECTED
#endif
#if defined(GETNCNT)
		VOID_RET(int, semctl(sem_id, 0, GETNCNT, 0));

		/* Exercise with probably an illegal sem id */
		VOID_RET(int, semctl(0x1fffffff, 0, GETNCNT, 0));

		/* Exercise with an illegal sem number */
		VOID_RET(int, semctl(sem_id, ~0, GETNCNT, 0));
#else
		UNEXPECTED
#endif
#if defined(GETZCNT)
		VOID_RET(int, semctl(sem_id, 0, GETZCNT, 0));

		/* Exercise with probably an illegal sem id */
		VOID_RET(int, semctl(0x1fffffff, 0, GETZCNT, 0));

		/* Exercise with an illegal sem number */
		VOID_RET(int, semctl(sem_id, ~0, GETZCNT, 0));
#else
		UNEXPECTED
#endif
		/*
		 * Now exercise invalid options and arguments
		 */
		VOID_RET(int, semctl(sem_id, -1, SETVAL, 0));
#if defined(HAVE_SEMTIMEDOP)
		if (got_semtimedop) {
			/*
			 *  Exercise illegal timeout
			 */
			struct sembuf semwait;
			struct timespec timeout;

			timeout.tv_sec = -1;
			timeout.tv_nsec = -1;
			semwait.sem_num = 0;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;
			VOID_RET(int, semtimedop(sem_id, &semwait, 1, &timeout));

			/*
			 *  Exercise invalid semid, EINVAL
			 */
			timeout.tv_sec = 0;
			timeout.tv_nsec = 10000;
			VOID_RET(int, semtimedop(-1, &semwait, 1, &timeout));

			/*
			 *  Exercise invalid nsops, E2BIG
			 */
			timeout.tv_sec = 0;
			timeout.tv_nsec = 10000;
			VOID_RET(int, semtimedop(sem_id, &semwait, (size_t)-1, &timeout));
			if (UNLIKELY(!stress_continue(args)))
				break;
		}
#else
		UNEXPECTED
#endif
		{
			struct sembuf semwait;
			/*
			 *  Exercise invalid semid, EINVAL
			 */
			VOID_RET(int, semop(-1, &semwait, 1));

			/*
			 *  Exercise invalid nsops, E2BIG
			 */
#if defined(__DragonFly__)
			VOID_RET(int, semop(sem_id, &semwait, (unsigned int)-1));
#else
			VOID_RET(int, semop(sem_id, &semwait, (size_t)-1));
#endif
		}

		/*
		 *  Exercise illegal semwait
		 */
		{
			struct sembuf semwait;

			semwait.sem_num = (unsigned short)-1;
			semwait.sem_op = -1;
			semwait.sem_flg = SEM_UNDO;

			VOID_RET(int, semop(sem_id, &semwait, 1));
		}

		/*
		 *  Exercise illegal cmd, use direct system call
		 *  if available as some libcs detect that the cmd
		 *  is invalid without doing the actual system call
		 */
		(void)semctl(sem_id, 0, INT_MAX, 0);
#if defined(__NR_semctl) &&	\
    defined(HAVE_SYSCALL)
		(void)syscall(__NR_semctl, sem_id, 0, INT_MAX, 0);
#endif
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	return rc;
}

/*
 *  semaphore_sysv_spawn()
 *	spawn a process
 */
static pid_t semaphore_sysv_spawn(const stress_args_t *args)
{
	pid_t pid;

again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		return -1;
	}
	if (pid == 0) {
		stress_parent_died_alarm();
		(void)sched_settings_apply(true);

		_exit(stress_semaphore_sysv_thrash(args));
	}
	return pid;
}

/*
 *  stress_sem_sysv()
 *	stress system by sem ops
 */
static int stress_sem_sysv(const stress_args_t *args)
{
	pid_t pids[MAX_SEM_SYSV_PROCS];
	uint64_t i;
	uint64_t semaphore_sysv_procs = DEFAULT_SEM_SYSV_PROCS;
	int rc = EXIT_SUCCESS;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	if (!stress_get_setting("sem-sysv-procs", &semaphore_sysv_procs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			semaphore_sysv_procs = MAX_SEM_SYSV_PROCS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			semaphore_sysv_procs = MIN_SEM_SYSV_PROCS;
	}

	if (!g_shared->sem_sysv.init) {
		pr_err("%s: aborting, semaphore not initialised\n", args->name);
		return EXIT_FAILURE;
	}

	if (stress_sighandler(args->name, SIGCHLD, stress_sighandler_nop, NULL) < 0)
		return EXIT_NO_RESOURCE;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	(void)shim_memset(pids, 0, sizeof(pids));
	for (i = 0; i < semaphore_sysv_procs; i++) {
		pids[i] = semaphore_sysv_spawn(args);
		if (!stress_continue_flag() || pids[i] < 0)
			goto reap;
	}

	/* Wait for termination */
	while (stress_continue(args))
		pause();
reap:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	stress_kill_and_wait_many(args, pids, semaphore_sysv_procs, SIGALRM, true);

	return rc;
}

stressor_info_t stress_sem_sysv_info = {
	.stressor = stress_sem_sysv,
	.init = stress_semaphore_sysv_init,
	.deinit = stress_semaphore_sysv_deinit,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sem_sysv_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_OS | CLASS_SCHEDULER,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/sem.h"
};
#endif
