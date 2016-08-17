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

#include "stress-ng.h"

#if defined(STRESS_MMAPFORK)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/sysinfo.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <setjmp.h>

#define MAX_PIDS		(32)

#define _EXIT_FAILURE			(0x01)
#define _EXIT_SEGV_MMAP			(0x02)
#define _EXIT_SEGV_MADV_WILLNEED	(0x04)
#define _EXIT_SEGV_MADV_DONTNEED	(0x08)
#define _EXIT_SEGV_MEMSET		(0x10)
#define _EXIT_SEGV_MUNMAP		(0x20)
#define _EXIT_MASK	(_EXIT_SEGV_MMAP | \
			 _EXIT_SEGV_MADV_WILLNEED | \
			 _EXIT_SEGV_MADV_DONTNEED | \
			 _EXIT_SEGV_MEMSET | \
			 _EXIT_SEGV_MUNMAP)

static volatile int segv_ret;

/*
 *  stress_segvhandler()
 *      SEGV handler
 */
static void MLOCKED stress_segvhandler(int dummy)
{
	(void)dummy;

	_exit(segv_ret);
}

/*
 *  stress_mmapfork()
 *	stress mappings + fork VM subystem
 */
int stress_mmapfork(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pids[MAX_PIDS];
	struct sysinfo info;
	void *ptr;
	uint64_t segv_count = 0;
	int32_t instances;
	int8_t segv_reasons = 0;

	(void)instance;

	if ((instances = stressor_instances(STRESS_MMAPFORK)) < 1)
		instances = stress_get_processors_configured();

	do {
		size_t i, n, len;

		memset(pids, 0, sizeof(pids));

		for (n = 0; n < MAX_PIDS; n++) {
retry:			if (!opt_do_run)
				goto reap;

			pids[n] = fork();
			if (pids[n] < 0) {
				/* Out of resources for fork, re-do, ugh */
				if (errno == EAGAIN) {
					usleep(10000);
					goto retry;
				}
				break;
			}
			if (pids[n] == 0) {
				/* Child */
				setpgid(0, pgrp);
				stress_parent_died_alarm();

				if (stress_sighandler(name, SIGSEGV, stress_segvhandler, NULL) < 0)
					_exit(_EXIT_FAILURE);

				if (sysinfo(&info) < 0) {
					pr_fail_err(name, "sysinfo");
					_exit(_EXIT_FAILURE);
				}

				len = ((size_t)info.freeram / (instances * MAX_PIDS)) / 2;
				segv_ret = _EXIT_SEGV_MMAP;
				ptr = mmap(NULL, len, PROT_READ | PROT_WRITE,
					MAP_POPULATE | MAP_SHARED | MAP_ANONYMOUS, -1, 0);
				if (ptr != MAP_FAILED) {
					segv_ret = _EXIT_SEGV_MADV_WILLNEED;
					madvise(ptr, len, MADV_WILLNEED);

					segv_ret = _EXIT_SEGV_MEMSET;
					memset(ptr, 0, len);

					segv_ret = _EXIT_SEGV_MADV_DONTNEED;
					madvise(ptr, len, MADV_DONTNEED);

					segv_ret = _EXIT_SEGV_MUNMAP;
					munmap(ptr, len);
				}
				_exit(EXIT_SUCCESS);
			}
			setpgid(pids[n], pgrp);
		}
reap:
		for (i = 0; i < n; i++) {
			int status;

			if (waitpid(pids[i], &status, 0) < 0) {
				if (errno != EINTR) {
					pr_err(stderr, "%s: waitpid errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
			} else {
				if (WIFEXITED(status)) {
					int masked = WEXITSTATUS(status) & _EXIT_MASK;

					if (masked) {
						segv_count++;
						segv_reasons |= masked;
					}
				}
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (segv_count) {
		char buffer[1024];

		*buffer = '\0';

		if (segv_reasons & _EXIT_SEGV_MMAP)
			strncat(buffer, " mmap", sizeof(buffer));
		if (segv_reasons & _EXIT_SEGV_MADV_WILLNEED)
			strncat(buffer, " madvise-WILLNEED", sizeof(buffer));
		if (segv_reasons & _EXIT_SEGV_MADV_DONTNEED)
			strncat(buffer, " madvise-DONTNEED", sizeof(buffer));
		if (segv_reasons & _EXIT_SEGV_MEMSET)
			strncat(buffer, " memset", sizeof(buffer));
		if (segv_reasons & _EXIT_SEGV_MUNMAP)
			strncat(buffer, " munmap", sizeof(buffer));

                pr_dbg(stderr, "%s: SIGSEGV errors: %" PRIu64 " (where:%s)\n",
			name, segv_count, buffer);
	}

	return EXIT_SUCCESS;
}

#endif
