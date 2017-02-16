/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

/*
 *  stress_brk()
 *	stress brk and sbrk
 */
int stress_brk(const args_t *args)
{
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, nomems = 0;
	const size_t page_size = args->page_size;

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, ret;

		(void)setpgid(pid, g_pgrp);
		/* Parent, wait for child */
		ret = waitpid(pid, &status, 0);
		if (ret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM "
					"killer, restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				ooms++;
				goto again;
			}
			/* If we got killed by sigsegv, re-start */
			if (WTERMSIG(status) == SIGSEGV) {
				pr_dbg("%s: killed by SIGSEGV, "
					"restarting again "
					"(instance %d)\n",
					args->name, args->instance);
				segvs++;
				goto again;
			}
		}
	} else if (pid == 0) {
		uint8_t *start_ptr;
		bool touch = !(g_opt_flags & OPT_FLAGS_BRK_NOTOUCH);
		int i = 0;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		start_ptr = sbrk(0);
		if (start_ptr == (void *) -1) {
			pr_fail_err("sbrk(0)");
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
						pr_err("%s: brk(%p) failed: errno=%d (%s)\n",
							args->name, start_ptr, errno,
							strerror(errno));
						exit(EXIT_FAILURE);
					}
				} else {
					pr_err("%s: sbrk(%d) failed: errno=%d (%s)\n",
						args->name, (int)page_size, errno,
						strerror(errno));
					exit(EXIT_FAILURE);
				}
			} else {
				/* Touch page, force it to be resident */
				if (touch)
					*(ptr - 1) = 0;
			}
			inc_counter(args);
		} while (keep_stressing());
	}
	if (ooms + segvs + nomems > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			args->name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}
