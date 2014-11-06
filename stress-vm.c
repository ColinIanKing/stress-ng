/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <signal.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "stress-ng.h"

/*
 *  stress_vm()
 *	stress virtual memory
 */
int stress_vm(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	uint32_t restarts = 0, nomems = 0;
	uint8_t *buf = NULL;
	uint8_t	val = 0;
	size_t	i;
	pid_t pid;
	const bool keep = (opt_flags & OPT_FLAGS_VM_KEEP);

	(void)instance;

again:
	if (!opt_do_run)
		return EXIT_SUCCESS;
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
				pr_dbg(stderr, "%s: waitpid(): errno=%d (%s)\n", name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
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
		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		do {
			const uint8_t gray_code = (val >> 1) ^ val;
			val++;

			if (!keep || (keep && buf == NULL)) {
				if (!opt_do_run)
					return EXIT_SUCCESS;
				buf = mmap(NULL, opt_vm_bytes, PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS | opt_vm_flags, -1, 0);
				if (buf == MAP_FAILED)
					continue;	/* Try again */

				(void)madvise_random(buf, opt_vm_bytes);
			}

			for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
				if (!opt_do_run)
					goto unmap_cont;
				*(buf + i) = gray_code;
			}
			(void)mincore_touch_pages(buf, opt_vm_bytes);

			if (opt_vm_hang == 0) {
				for (;;)
					(void)sleep(3600);
			} else if (opt_vm_hang != DEFAULT_VM_HANG) {
				(void)sleep((int)opt_vm_hang);
			}

			for (i = 0; i < opt_vm_bytes; i += opt_vm_stride) {
				if (!opt_do_run)
					goto unmap_cont;
				if (*(buf + i) != gray_code) {
					if (opt_flags & OPT_FLAGS_VERIFY) {
						pr_fail(stderr, "%s: detected memory error, offset : %zd, got: %x\n",
							name, i, *(buf + i));
						break;
					} else {
						pr_err(stderr, "%s: detected memory error, offset : %zd, got: %x\n",
							name, i, *(buf + i));
						(void)munmap(buf, opt_vm_bytes);
						return EXIT_FAILURE;
					}
				}
				if (!opt_do_run)
					goto unmap_cont;
			}
unmap_cont:
			if (!keep) {
				(void)madvise_random(buf, opt_vm_bytes);
				(void)munmap(buf, opt_vm_bytes);
			}

			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));

		if (keep && buf != NULL)
			(void)munmap(buf, opt_vm_bytes);
	}
	pr_dbg(stderr, "%s: OOM restarts: %" PRIu32 ", out of memory restarts: %" PRIu32 ".\n",
			name, restarts, nomems);

	return EXIT_SUCCESS;
}
