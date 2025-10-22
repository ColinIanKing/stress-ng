/*
 * Copyright (C) 2021-2025 Colin Ian King
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-killpid.h"
#include "core-mmap.h"

#include <sched.h>

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

static void OPTIMIZE3 NORETURN stress_resched_child(
	stress_args_t *args,
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
#if defined(SCHED_BATCH)
		SCHED_BATCH,
#endif
#if defined(SCHED_EXT)
		SCHED_EXT,
#endif
#if defined(SCHED_IDLE)
		SCHED_IDLE,
#endif
#if defined(SCHED_OTHER)
		SCHED_OTHER,
#endif
	};
#endif

	stress_parent_died_alarm();
	(void)sched_settings_apply(true);

	for (i = niceness; i < max_niceness; i++) {
		int k;
		register uint64_t *const yield_ptr = &yields[i];

		for (k = 0; k < 1024; k++) {
#if defined(HAVE_SCHEDULING) && 	\
    defined(HAVE_SCHED_SETSCHEDULER)
			struct sched_param param;
			size_t j;

			(void)shim_memset(&param, 0, sizeof(param));
			for (j = 0; j < SIZEOF_ARRAY(normal_policies); j++) {
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
				(void)shim_sched_yield();
				stress_bogo_inc(args);
				(*yield_ptr)++;
			}
#else
			(void)shim_sched_yield();
			stress_bogo_inc(args);
			(*yield_ptr)++;
#endif
		}

		VOID_RET(int, shim_nice(1));

		if (UNLIKELY(!stress_continue(args)))
			break;
	}
	_exit(rc);
}

/*
 *  stress_resched_spawn()
 *	start a child process that will re-nice itself
 */
static void stress_resched_spawn(
	stress_args_t *args,
	stress_pid_t *s_pids,
	const int idx,
	const int max_prio,
	uint64_t *yields)
{
	pid_t pid;

	s_pids[idx].pid = -1;

	pid = fork();
	if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_resched_child(args, idx, max_prio, yields);
	} else if (pid > 0) {
		s_pids[idx].pid = pid;
	}
}

/*
 *  stress on sched_resched()
 *	stress system by sched_resched
 */
static int stress_resched(stress_args_t *args)
{
	stress_pid_t *s_pids;
	int i, s_pids_max, max_prio = 19, rc = EXIT_SUCCESS;
	size_t yields_size;
	uint64_t *yields;

#if defined(HAVE_SETPRIORITY) &&	\
    defined(RLIMIT_NICE)
	{
		struct rlimit rlim;

		if (getrlimit(RLIMIT_NICE, &rlim) == 0) {
			max_prio = 20 - (int)rlim.rlim_cur;
		}
	}
#endif
	s_pids_max = max_prio + 1; /* 0.. max_prio */
	s_pids = stress_sync_s_pids_mmap((size_t)s_pids_max);
	if (s_pids == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d PIDs%s, skipping stressor\n",
			args->name, s_pids_max, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	for (i = 0; i < s_pids_max; i++)
		s_pids[i].pid = -1;

	if (stress_sighandler(args->name, SIGUSR1, stress_resched_usr1_handler, NULL) < 0) {
		rc = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}

	yields_size = ((sizeof(*yields) * (size_t)s_pids_max) + args->page_size - 1) & ~(args->page_size - 1);
	yields = (uint64_t *)stress_mmap_populate(NULL, yields_size,
				PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (yields == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte yield counter array%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, yields_size,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto tidy_s_pids;
	}
	stress_set_vma_anon_name(yields, yields_size, "yield-stats");

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	/* Start off one child process per positive nice level */
	for (i = 0; LIKELY(stress_continue(args) && (i < s_pids_max)); i++)
		stress_resched_spawn(args, s_pids, i, max_prio, yields);

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
			for (i = 0; i < s_pids_max; i++) {
				if ((s_pids[i].pid == -1) || (pid == s_pids[i].pid))
					stress_resched_spawn(args, s_pids, i, max_prio, yields);
			}
		}
	} while (stress_continue(args));

	if (stress_kill_and_wait_many(args, s_pids, s_pids_max, SIGALRM, true) == EXIT_FAILURE)
		rc = EXIT_FAILURE;
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	/*
	 *  Dump stats for just instance 0 to reduce output
	 */
	if (stress_instance_zero(args)) {
		uint64_t total_yields = 0;

		for (i = 0; i < s_pids_max; i++)
			total_yields += yields[i];

		pr_block_begin();
		for (i = 0; i < s_pids_max; i++) {
			if (yields[i] > 0) {
				const double percent = 100.0 * ((double)yields[i]/ (double)total_yields);

				if (i == 0) {
					pr_dbg("%s: prio %2d: %5.2f%% yields\n",
						args->name, i, percent);
				} else {
					const double scale = (double)yields[i] / (double)yields[i - 1];

					pr_dbg("%s: prio %2d: %5.2f%% yields (prio %2d x %f)%s\n",
						args->name, i, percent, i - 1, scale,
						(scale < 1.0) ? " bad" : "");
				}
			}
		}
		pr_block_end();
	}

	(void)munmap((void *)yields, yields_size);
tidy_s_pids:
	(void)stress_sync_s_pids_munmap(s_pids, (size_t)s_pids_max);

	return rc;
}

const stressor_info_t stress_resched_info = {
	.stressor = stress_resched,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else
const stressor_info_t stress_resched_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without Linux scheduling support"
};
#endif
