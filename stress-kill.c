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
 	{ NULL,	"kill N",	"start N workers killing with SIGUSR1" },
	{ NULL,	"kill-ops N",	"stop after N kill bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress on sched_kill()
 *	stress system by rapid kills
 */
static int stress_kill(const args_t *args)
{
	uint64_t udelay = 5000;

	if (stress_sighandler(args->name, SIGUSR1, SIG_IGN, NULL) < 0)
		return EXIT_FAILURE;

	do {
		int ret;

		/*
		 *  With many kill stressors we get into a state
		 *  where they all hammer on kill system calls and
		 *  this stops the parent from getting scheduling
		 *  time to spawn off the rest of the kill stressors
		 *  causing some lag in getting all the stressors
		 *  running. Ease this pressure off to being with
		 *  with some small sleeps that shrink to zero over
		 *  time. The alternative was to re-nice all the
		 *  processes, but even then one gets the child
		 *  stressors all contending and causing a bottle
		 *  neck.  Any simpler and/or better solutions would
		 *  be appreciated!
		 */
		if (udelay >= 1000) {
			(void)shim_usleep(udelay);
			udelay -= 500;
		}

		ret = kill(args->pid, SIGUSR1);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail_err("kill");

		/* Zero signal can be used to see if process exists */
		ret = kill(args->pid, 0);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail_err("kill");

		/*
		 * Zero signal can be used to see if process exists,
		 * -1 pid means signal sent to every process caller has
		 * permission to send to
		 */
		ret = kill(-1, 0);
		if ((ret < 0) && (g_opt_flags & OPT_FLAGS_VERIFY))
			pr_fail_err("kill");

		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}

stressor_info_t stress_kill_info = {
	.stressor = stress_kill,
	.class = CLASS_INTERRUPT | CLASS_SCHEDULER | CLASS_OS,
	.help = help
};
