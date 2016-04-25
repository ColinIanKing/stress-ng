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
	uint32_t ooms= 0, segvs = 0, nomems = 0;
	const size_t page_size = stress_get_pagesize();

again:
	if (!opt_do_run)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		setpgid(pid, pgrp);
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n",
					name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %s (instance %d)\n",
				name, stress_strsignal(WTERMSIG(status)),
				instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg(stderr, "%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					name, instance);
				ooms++;
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg(stderr, "%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					name, instance);
				segvs++;
				goto again;
			}
		}
	} else if (pid == 0) {
		uint8_t *start_ptr;
		bool touch = !(opt_flags & OPT_FLAGS_BRK_NOTOUCH);
		int i = 0;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		start_ptr = sbrk(0);
		if (start_ptr == (void *) -1) {
			pr_err(stderr, "%s: sbrk(0) failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
			exit(EXIT_FAILURE);
		}

		do {
			uint8_t *ptr;

			i++;
			if (i > 8) {
				i = 0;
				ptr = sbrk(0);
				ptr -= page_size;
				if (brk(ptr) < 0)
					ptr = (void *)-1;
			} else {
				ptr = sbrk((intptr_t)page_size);
			}
			if (ptr == (void *)-1) {
				if ((errno == ENOMEM) || (errno == EAGAIN)) {
					nomems++;
					if (brk(start_ptr) < 0) {
						pr_err(stderr, "%s: brk(%p) failed: errno=%d (%s)\n",
							name, start_ptr, errno,
							strerror(errno));
						exit(EXIT_FAILURE);
					}
				} else {
					pr_err(stderr, "%s: sbrk(%d) failed: errno=%d (%s)\n",
						name, (int)page_size, errno,
						strerror(errno));
					exit(EXIT_FAILURE);
				}
			} else {
				/* Touch page, force it to be resident */
				if (touch)
					*(ptr - 1) = 0;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}
	if (ooms + segvs + nomems > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}
