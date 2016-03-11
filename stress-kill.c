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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <signal.h>

#include "stress-ng.h"

/*
 *  stress on sched_kill()
 *	stress system by rapid kills
 */
int stress_kill(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();

	(void)instance;

	if (stress_sighandler(name, SIGUSR1, SIG_IGN, NULL) < 0)
		return EXIT_FAILURE;

	do {
		int ret;

		ret = kill(pid, SIGUSR1);
		if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
			pr_fail(stderr, "%s: kill failed: errno=%d (%s)\n",
				name, errno, strerror(errno));

		/* Zero signal can be used to see if process exists */
		ret = kill(pid, 0);
		if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
			pr_fail(stderr, "%s: kill failed: errno=%d (%s)\n",
				name, errno, strerror(errno));

		/*
		 * Zero signal can be used to see if process exists,
		 * -1 pid means signal sent to every process caller has
		 * permission to send to
		 */
		ret = kill(-1, 0);
		if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY))
			pr_fail(stderr, "%s: kill failed: errno=%d (%s)\n",
				name, errno, strerror(errno));

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
