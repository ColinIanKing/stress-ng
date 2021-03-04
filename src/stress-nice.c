/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"nice N",	"start N workers that randomly re-adjust nice levels" },
	{ NULL,	"nice-ops N",	"stop after N nice bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_NICE) || defined(HAVE_SETPRIORITY)

static void stress_nice_delay(void)
{
	double start = stress_time_now();
	double delay = 0.01 + (double)stress_mwc16() / 3276800.0;

	while (stress_time_now() - start < delay)
		(void)shim_sched_yield();
}

/*
 *  stress on sched_nice()
 *	stress system by sched_nice
 */
static int stress_nice(const stress_args_t *args)
{
	const bool cap_sys_nice = stress_check_capability(SHIM_CAP_SYS_NICE);
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
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		pid_t pid;
		int which = stress_mwc1();

		pid = fork();
		if (pid == 0) {
#if defined(HAVE_GETPRIORITY)
			static const int prio_which[] = {
				PRIO_PROCESS,
				PRIO_USER,
				PRIO_PGRP,
			};
#endif
			int i;

			/*
			 *  Test if calling process has CAP_SYS_NICE
			 *  capability then only it can increase
			 *  its priority by decreasing nice value
			 */
			if (!cap_sys_nice) {
				(void)shim_nice(-1);
			}

			/* Child */
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();
			(void)sched_settings_apply(true);
#if defined(HAVE_GETPRIORITY) &&	\
    defined(HAVE_SETPRIORITY)
			for (i = 0; i < (int)SIZEOF_ARRAY(prio_which); i++) {
				int ret;

				errno = 0;
				ret = getpriority(prio_which[i], 0);
				if ((errno == 0) && (!cap_sys_nice)) {
					/*
					 * Get priority returns a value that is in the
					 * range 40..1 for -20..19, so negate and offset
					 * by 20 to get back into setpriority prio level
					 */
					ret = setpriority(prio_which[i], 0, -ret + 20);
				}

				(void)ret;
			}
#endif

#if defined(HAVE_SETPRIORITY)
			/*
			 *  Exercise setpriority calls that uses illegal
			 *  arguments to get more kernel test coverage
			 */
			(void)setpriority(INT_MIN, 0, max_prio - 1);
			(void)setpriority(INT_MAX, 0, max_prio - 1);
#endif

			switch (which) {
#if defined(HAVE_SETPRIORITY)
			case 1:
				pid = getpid();
				for (i = min_prio; (i <= max_prio) && keep_stressing(args); i++) {
					errno = 0;
					(void)setpriority(PRIO_PROCESS, pid, i);
					if (!errno)
						stress_nice_delay();
					inc_counter(args);
				}
				break;
#endif
			default:
				for (i = -19; (i < 20) && keep_stressing(args); i++) {
					int ret;

					ret = shim_nice(1);
					if (ret == 0)
						stress_nice_delay();
					inc_counter(args);
				}
				break;
			}
			_exit(0);
		}
		if (pid > 0) {
			int status;

			(void)setpgid(pid, g_pgrp);

			/* Parent, wait for child */
			if (shim_waitpid(pid, &status, 0) < 0) {
				(void)kill(pid, SIGTERM);
				(void)kill(pid, SIGKILL);
			}
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_nice_info = {
	.stressor = stress_nice,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};

#else
stressor_info_t stress_nice_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
#endif
