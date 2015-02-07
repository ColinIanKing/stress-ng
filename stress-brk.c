/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <inttypes.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
int stress_brk(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	uint32_t restarts = 0, nomems = 0;
	const size_t page_size = stress_get_pagesize();

again:
	pid = fork();
	if (pid < 0) {
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %d (instance %d)\n",
				name, WTERMSIG(status), instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				pr_dbg(stderr, "%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					name, instance);
				restarts++;
				goto again;
			}
		}
	} else if (pid == 0) {
		uint8_t *ptr, *start_ptr;
		bool touch = !(opt_flags & OPT_FLAGS_BRK_NOTOUCH);

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		start_ptr = sbrk(0);
		if (start_ptr == (void *) -1) {
			pr_err(stderr, "%s: sbrk(0) failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		do {
			ptr = sbrk((intptr_t)page_size);
			if (ptr == (void *)-1) {
				if (errno == ENOMEM) {
					nomems++;
					if (brk(start_ptr) < 0) {
						pr_err(stderr, "%s: brk(%p) failed: errno=%d (%s)\n",
							name, start_ptr, errno, strerror(errno));
						exit(EXIT_FAILURE);
					}
				} else {
					pr_err(stderr, "%s: sbrk(%d) failed: errno=%d (%s)\n",
						name, (int)page_size, errno, strerror(errno));
					exit(EXIT_FAILURE);
				}
			} else {
				/* Touch page, force it to be resident */
				if (touch)
					*(ptr - 1) = 0;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
		free(ptr);
	}
	if (restarts + nomems > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32 ", out of memory restarts: %" PRIu32 ".\n",
			name, restarts, nomems);

	return EXIT_SUCCESS;
}
