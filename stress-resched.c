// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"resched N",		"start N workers that spawn renicing child processes" },
	{ NULL,	"resched-ops N",	"stop after N nice bogo nice'd yield operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_NICE)

#if (defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)) &&	     \
    (defined(SCHED_OTHER) || defined(SCHED_BATCH) || defined(SCHED_IDLE)) && \
     !defined(__OpenBSD__) &&						     \
     !defined(__minix__) &&						     \
     !defined(__APPLE__)
#define HAVE_SCHEDULING
#endif

static void MLOCKED_TEXT stress_resched_usr1_handler(int sig)
{
	if (sig == SIGUSR1)
		stress_continue_set_flag(false);
}

static void NORETURN stress_resched_child(
	const stress_args_t *args,
	const int niceness,
	const int max_niceness,
	uint64_t *yields)
{
	int rc = EXIT_SUCCESS;
	int i;
#if defined(HAVE_SCHEDULING) &&		\
    defined(HAVE_SCHED_SETSCHEDULER)
	const pid_t pid = getpid();

	/*
	 *  "Normal" non-realtime scheduling policies
	 */
	static const int normal_policies[] = {
#if defined(SCHED_OTHER)
		SCHED_OTHER,
#endif
#if defined(SCHED_BATCH)
		SCHED_BATCH,
#endif
#if defined(SCHED_IDLE)
		SCHED_IDLE,
#endif
	};
#endif

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	for (i = niceness; i < max_niceness; i++) {
		int k;

		for (k = 0; k < 1024; k++) {
#if defined(HAVE_SCHEDULING) && 	\
    defined(HAVE_SCHED_SETSCHEDULER)
			size_t j;

			for (j = 0; j < SIZEOF_ARRAY(normal_policies); j++) {
				struct sched_param param;

				if (!stress_continue(args))
					break;

				(void)shim_memset(&param, 0, sizeof(param));
				param.sched_priority = 0;
				if (sched_setscheduler(pid, normal_policies[j], &param) == 0) {
					int ret;

					/* Is the scheduler different from the one set? */
					ret = sched_getscheduler(pid);
					if ((ret >= 0) && (ret != normal_policies[j])) {
						pr_fail("%s: current scheduler %d different from the set scheduler %d\n",
							args->name, ret, normal_policies[j]);
						/* tell parent it's time to stop */
						(void)shim_kill(args->pid, SIGUSR1);
						_exit(EXIT_FAILURE);
					}
				}
				VOID_RET(int, shim_sched_yield());
				if (yields)
					yields[i]++;
				stress_bogo_inc(args);
			}
#else
			VOID_RET(int, shim_sched_yield());
			if (yields)
				yields[i]++;
			stress_bogo_inc(args);
#endif
		}

		VOID_RET(int, nice(1));

		if (!stress_continue(args))
			break;
	}
	_exit(rc);
}

/*
 *  stress_resched_spawn()
 *	start a child process that will re-nice itself
 */
static void stress_resched_spawn(
	const stress_args_t *args,
	pid_t *pids,
	const int index,
	const int max_prio,
	uint64_t *yields)
{
	pid_t pid;

	pids[index] = -1;

	pid = fork();
	if (pid == 0) {
		stress_resched_child(args, index, max_prio, yields);
	} else if (pid > 0) {
		pids[index] = pid;
	}
}

/*
 *  stress on sched_resched()
 *	stress system by sched_resched
 */
static int stress_resched(const stress_args_t *args)
{
	pid_t *pids;
	int rc = EXIT_SUCCESS;

#if defined(HAVE_SETPRIORITY)
	int i, pids_max, max_prio = 19;
	size_t yields_size;
	uint64_t *yields;

#if defined(RLIMIT_NICE)
	{
		struct rlimit rlim;

		if (getrlimit(RLIMIT_NICE, &rlim) == 0) {
			max_prio = 20 - (int)rlim.rlim_cur;
		}
	}
#endif
#endif
	pids_max = max_prio + 1; /* 0.. max_prio */
	pids = calloc((size_t)pids_max, sizeof(*pids));
	if (!pids) {
		pr_inf_skip("%s: cannot allocate pids array, skipping stressor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < pids_max; i++)
		pids[i] = -1;

	if (stress_sighandler(args->name, SIGUSR1, stress_resched_usr1_handler, NULL) < 0) {
		rc = EXIT_NO_RESOURCE;
		goto free_pids;
	}

	yields_size = ((sizeof(*yields) * (size_t)pids_max) + args->page_size - 1) & ~(args->page_size - 1);
	yields = (uint64_t *)mmap(NULL, yields_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (yields == MAP_FAILED)
		yields = NULL;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* Start off one child process per positive nice level */
	for (i = 0; stress_continue(args) && (i < pids_max); i++)
		stress_resched_spawn(args, pids, i, max_prio, yields);

	do {
		pid_t pid;
		int status;

		/* Wait for child processes to die */
		pid = wait(&status);
		if (pid >= 0) {
			if (WIFEXITED(status) && (WEXITSTATUS(status) == EXIT_FAILURE)) {
				break;
			}
			/*
			 *  Find unstarted process or process that just terminated
			 *  and respawn it at the nice level of the given slot
			 */
			for (i = 0; i < pids_max; i++) {
				if ((pids[i] == -1) || (pid == pids[i]))
					stress_resched_spawn(args, pids, i, max_prio, yields);
			}
		}
	} while (stress_continue(args));

	if (stress_kill_and_wait_many(args, pids, pids_max, SIGALRM, true) == EXIT_FAILURE)
		rc = EXIT_FAILURE;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  Dump stats for just instance 0 to reduce output
	 */
	if ((yields != NULL) && (args->instance == 0)) {
		uint64_t total_yields = 0;

		for (i = 0; i < pids_max; i++)
			total_yields += yields[i];

		pr_block_begin();
		for (i = 0; i < pids_max; i++) {
			if (yields[i] > 0) {
				double percent = 100.0 * ((double)yields[i] / (double)total_yields);

				if (i == 0) {
					pr_dbg("%s: prio %2d: %5.2f%% yields\n",
						args->name, i, percent);
				} else {
					double scale = (double)yields[i] / (double)yields[i - 1];

					pr_dbg("%s: prio %2d: %5.2f%% yields (prio %2d x %f)%s\n",
						args->name, i, percent, i - 1, scale,
						(scale < 1.0) ? " bad" : "");
				}
			}
		}
		pr_block_end();
	}

	if (yields != NULL)
		(void)munmap((void *)yields, yields_size);
free_pids:
	free(pids);

	return rc;
}

stressor_info_t stress_resched_info = {
	.stressor = stress_resched,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else
stressor_info_t stress_resched_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
