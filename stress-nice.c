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
	{ NULL,	"nice N",	"start N workers that randomly re-adjust nice levels" },
	{ NULL,	"nice-ops N",	"stop after N nice bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_NICE) || defined(HAVE_SETPRIORITY)

static void stress_nice_delay(void)
{
	double start = time_now();
	double delay = 0.01 + (double)mwc16() / 3276800.0;

	while (time_now() - start < delay)
		(void)shim_sched_yield();
}

/*
 *  stress on sched_nice()
 *	stress system by sched_nice
 */
static int stress_nice(const args_t *args)
{
#if defined(HAVE_SETPRIORITY)
	int max_prio, min_prio;

#if defined(RLIMIT_NICE)
	{
		struct rlimit rlim;

		if (getrlimit(RLIMIT_NICE, &rlim) < 0) {
			/* Make an assumption, bah */
			max_prio = 20;
			min_prio = -20;
		} else {
			max_prio = 20 - (int)rlim.rlim_cur;
			min_prio = -max_prio;
		}
	}
#else
	/* Make an assumption, bah */
	max_prio = 20;
	min_prio = -20;
#endif
#endif

	do {
		pid_t pid;
		int which = mwc1();

		pid = fork();
		if (pid == 0) {
			int i;

			/* Child */
			(void)setpgid(0, g_pgrp);
			stress_parent_died_alarm();

			switch (which) {
#if defined(HAVE_SETPRIORITY)
			case 1:
				pid = getpid();
				for (i = min_prio; (i <= max_prio) && keep_stressing(); i++) {
					errno = 0;
					(void)setpriority(PRIO_PROCESS, pid, i);
					if (!errno)
						stress_nice_delay();
					inc_counter(args);
				}
				break;
#endif
			default:
				for (i = -19; i < 20; i++) {
					int ret;

					ret = nice(1);
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
	} while (keep_stressing());

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
