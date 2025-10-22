/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-capabilities.h"
#include "core-killpid.h"

static const stress_help_t help[] = {
	{ NULL,	"nice N",	"start N workers that randomly re-adjust nice levels" },
	{ NULL,	"nice-ops N",	"stop after N nice bogo operations" },
	{ NULL,	NULL,		NULL }
};

#define BAD_PRIO	(-1000)

#if defined(HAVE_NICE) || defined(HAVE_SETPRIORITY)

static void stress_nice_delay(void)
{
	const double start = stress_time_now();
	const uint16_t r = stress_mwc16();
	const double delay = 0.01 + (double)r / 3276800.0;

	while ((stress_time_now() - start) < delay)
		(void)shim_sched_yield();
}

/*
 *  stress on sched_nice()
 *	stress system by sched_nice
 */
static int stress_nice(stress_args_t *args)
{
	const bool cap_sys_nice = stress_check_capability(SHIM_CAP_SYS_NICE);
#if defined(HAVE_SETPRIORITY) || 	\
    defined(HAVE_GETPRIORITY)
#endif
	int rc = EXIT_SUCCESS;
#if defined(HAVE_SETPRIORITY)
	/* Make an assumption on priority range */
	int max_prio = 20, min_prio = -20;

#if defined(RLIMIT_NICE)
	{
		struct rlimit rlim;

		max_prio = 20;
		min_prio = -20;

		if ((getrlimit(RLIMIT_NICE, &rlim) == 0) &&
		    (rlim.rlim_cur != 0))  {
			max_prio = 20 - (int)rlim.rlim_cur;
			min_prio = -max_prio;
		}
	}
#endif
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		const int which = stress_mwc1();

		pid = fork();
		if (pid == 0) {
#if defined(HAVE_GETPRIORITY) &&	\
    defined(HAVE_SETPRIORITY)
			static const shim_priority_which_t prio_which[] = {
				PRIO_PROCESS,
				PRIO_USER,
				PRIO_PGRP,
			};
#endif
			int i;

			stress_set_proc_state(args->name, STRESS_STATE_RUN);

			/*
			 *  Test if calling process has CAP_SYS_NICE
			 *  capability then only it can increase
			 *  its priority by decreasing nice value
			 */
			if (!cap_sys_nice) {
				(void)shim_nice(-1);
			}

			/* Child */
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
#if defined(HAVE_GETPRIORITY) &&	\
    defined(HAVE_SETPRIORITY)
			for (i = 0; i < (int)SIZEOF_ARRAY(prio_which); i++) {
				const shim_priority_which_t prio = prio_which[i];
				int ret;

				errno = 0;
				ret = getpriority(prio, 0);

				/* Only set priority on child process */
				if ((prio == PRIO_PROCESS) && (errno == 0) && (!cap_sys_nice))
					VOID_RET(int, setpriority(prio, 0, ret));
			}
#endif

#if defined(HAVE_SETPRIORITY)
			/*
			 *  Exercise setpriority calls that uses illegal
			 *  arguments to get more kernel test coverage
			 */
			(void)setpriority((shim_priority_which_t)INT_MIN, 0, max_prio - 1);
			(void)setpriority((shim_priority_which_t)INT_MIN, -1, max_prio - 1);
			(void)setpriority((shim_priority_which_t)INT_MAX, 0, max_prio - 1);
			(void)setpriority((shim_priority_which_t)INT_MAX, -1, max_prio - 1);
			(void)setpriority(PRIO_PROCESS, -1, max_prio - 1);
			(void)setpriority(PRIO_USER, -1, max_prio - 1);
			(void)setpriority(PRIO_PGRP, -1, max_prio - 1);
#endif

			switch (which) {
#if defined(HAVE_SETPRIORITY)
			case 1:
				pid = getpid();
				for (i = min_prio; LIKELY((i <= max_prio) && stress_continue(args)); i++) {
					errno = 0;
					(void)setpriority(PRIO_PROCESS, (id_t)pid, i);
					if (!errno)
						stress_nice_delay();
					stress_bogo_inc(args);
				}
				break;
#endif
			default:
				for (i = -19; LIKELY((i < 20) && stress_continue(args)); i++) {
					int ret;
#if defined(HAVE_GETPRIORITY)
					int old_prio, new_prio;

					errno = 0;
					old_prio = getpriority(PRIO_PROCESS, 0);
					if (errno < 0)
						old_prio = BAD_PRIO;
#endif

					ret = shim_nice(1);
#if defined(HAVE_GETPRIORITY)
					errno = 0;
					new_prio = getpriority(PRIO_PROCESS, 0);
					if (errno < 0)
						new_prio = BAD_PRIO;

					/* Sanity check priority change of nice(1) */
					if ((old_prio != BAD_PRIO) && (new_prio != BAD_PRIO)) {
						const int delta = new_prio - old_prio;

						if (delta > 1) {
							pr_fail("%s: nice(1) changed priority by 1, "
								"detected a priority change of %d "
								"instead\n", args->name, delta);
							rc = EXIT_FAILURE;
						}
					}
#endif
					if (ret == 0)
						stress_nice_delay();
					stress_bogo_inc(args);
				}
				break;
			}
			_exit(rc);
		}
		if (pid > 0) {
			int status;

			/* Parent, wait for child */
			if (shim_waitpid(pid, &status, 0) < 0) {
				stress_force_killed_bogo(args);
				(void)stress_kill_pid(pid);
			} else {
				if (WIFEXITED(status)) {
					if (WEXITSTATUS(status) != EXIT_SUCCESS) {
						rc = WEXITSTATUS(status);
						break;
					}
				}
			}
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
;
}

const stressor_info_t stress_nice_info = {
	.stressor = stress_nice,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else
const stressor_info_t stress_nice_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SCHEDULER | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without nice() or setpriority() system call support"
};
#endif
