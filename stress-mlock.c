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

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>

#include "stress-ng.h"

#define MLOCK_MAX	(256*1024)

/*
 *  stress_mlock()
 *	stress mlock with pages being locked/unlocked
 */
int stress_mlock(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	pid_t pid;
	size_t max = sysconf(_SC_MAPPED_FILES);
	max = max > MLOCK_MAX ? MLOCK_MAX : max;

	(void)instance;

again:
	pid = fork();
	if (pid < 0) {
		if (opt_do_run && (errno == EAGAIN))
			goto again;
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
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg(stderr, "%s: child died: %s (instance %d)\n",
				name, stress_strsignal(WTERMSIG(status)), instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				pr_dbg(stderr, "%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					name, instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		uint8_t *mappings[max];
		size_t i, n;

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			for (n = 0; opt_do_run && (n < max); n++) {
				int ret;
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;

				mappings[n] = mmap(NULL, page_size * 3,
					PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (mappings[n] == MAP_FAILED)
					break;
				ret = mlock(mappings[n] + page_size, page_size);
				if (ret < 0) {
					if (errno == EAGAIN)
						continue;
					if (errno == ENOMEM)
						break;
					pr_failed_err(name, "mlock");
					break;
				} else {
					/*
					 * Mappings are always page aligned so
					 * we can use the bottom bit to
					 * indicate if the page has been
					 * mlocked or not
				 	 */
					mappings[n] = (uint8_t *)
						((ptrdiff_t)mappings[n] | 1);
					(*counter)++;
				}
			}

			for (i = 0; i < n;  i++) {
				ptrdiff_t addr = (ptrdiff_t)mappings[i];
				ptrdiff_t mlocked = addr & 1;

				addr ^= mlocked;
				if (mlocked)
					(void)munlock((uint8_t *)addr + page_size, page_size);
				munmap((void *)addr, page_size * 3);
			}
#if !defined(__gnu_hurd__)
			(void)mlockall(MCL_CURRENT);
			(void)mlockall(MCL_FUTURE);
#endif
			for (n = 0; opt_do_run && (n < max); n++) {
				if (!opt_do_run || (max_ops && *counter >= max_ops))
					break;

				mappings[n] = mmap(NULL, page_size,
					PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
				if (mappings[n] == MAP_FAILED)
					break;
			}
#if !defined(__gnu_hurd__)
			(void)munlockall();
#endif
			for (i = 0; i < n;  i++)
				munmap(mappings[i], page_size);
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}

	return EXIT_SUCCESS;
}
