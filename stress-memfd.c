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

#if defined(STRESS_MEMFD)

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
#include <fcntl.h>

#define MAX_MEM_FDS 	(256)

static size_t opt_memfd_bytes = DEFAULT_MEMFD_BYTES;
static bool set_memfd_bytes;

void stress_set_memfd_bytes(const char *optarg)
{
	set_memfd_bytes = true;
	opt_memfd_bytes = (size_t)get_uint64_byte(optarg);
	check_range("memfd-bytes", opt_memfd_bytes,
		MIN_MEMFD_BYTES, MAX_MEMFD_BYTES);
}

/*
 *  Ugly hack until glibc defines this
 */
static inline int sys_memfd_create(const char *name, unsigned int flags)
{
#if defined(__NR_memfd_create)
	return syscall(__NR_memfd_create, name, flags);
#else
	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  Create allocations using memfd_create, ftruncate and mmap
 */
static void stress_memfd_allocs(
	const char *name,
	uint64_t *const counter,
	const uint64_t max_ops)
{
	int fds[MAX_MEM_FDS];
	void *maps[MAX_MEM_FDS];
	size_t i;
	const size_t page_size = stress_get_pagesize();
	const size_t min_size = 2 * page_size;
	size_t size = opt_memfd_bytes / MAX_MEM_FDS;
	const pid_t pid = getpid();

	if (size < min_size)
		size = min_size;

	do {
		for (i = 0; i < MAX_MEM_FDS; i++) {
			fds[i] = -1;
			maps[i] = MAP_FAILED;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
			char name[PATH_MAX];

			snprintf(name, sizeof(name), "memfd-%u-%zu", pid, i);
			fds[i] = sys_memfd_create(name, 0);
			if (fds[i] < 0) {
				switch (errno) {
				case EMFILE:
				case ENFILE:
					break;
				case ENOMEM:
					goto clean;
				case ENOSYS:
				case EFAULT:
				default:
					pr_err(stderr, "%s: memfd_create failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
					opt_do_run = false;
					goto clean;
				}
			}
			if (!opt_do_run)
				goto clean;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
			if (fds[i] >= 0) {
				ssize_t ret;
				size_t whence;

				if (!opt_do_run)
					break;

				/* Allocate space */
				ret = ftruncate(fds[i], size);
				if (ret < 0) {
					switch (errno) {
					case EINTR:
						break;
					default:
						pr_fail(stderr, "%s: ftruncate failed, errno=%d (%s)\n",
                                        		name, errno, strerror(errno));
						break;
					}
				}
				/*
				 * ..and map it in, using MAP_POPULATE
				 * to force page it in
				 */
				maps[i] = mmap(NULL, size, PROT_WRITE,
					MAP_FILE | MAP_SHARED | MAP_POPULATE,
					fds[i], 0);
				mincore_touch_pages(maps[i], size);
				madvise_random(maps[i], size);

#if defined(FALLOC_FL_PUNCH_HOLE) && defined(FALLOC_FL_KEEP_SIZE)
				/*
				 *  ..and punch a hole
				 */
				whence = (mwc32() % size) & ~(page_size - 1);
				ret = fallocate(fds[i], FALLOC_FL_PUNCH_HOLE |
					FALLOC_FL_KEEP_SIZE, whence, page_size);
				(void)ret;
#endif
			}
			if (!opt_do_run)
				goto clean;
		}

		for (i = 0; i < MAX_MEM_FDS; i++) {
#if defined(SEEK_SET)
			if (lseek(fds[i], (off_t)size>> 1, SEEK_SET) < 0) {
				if (errno != ENXIO)
					pr_fail(stderr, "%s: lseek SEEK_SET on memfd failed: "
						"errno=%d (%s)\n", name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_CUR)
			if (lseek(fds[i], (off_t)0, SEEK_CUR) < 0) {
				if (errno != ENXIO)
					pr_fail(stderr, "%s: lseek SEEK_CUR on memfd failed: "
						"errno=%d (%s)\n", name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_END)
			if (lseek(fds[i], (off_t)0, SEEK_END) < 0) {
				if (errno != ENXIO)
					pr_fail(stderr, "%s: lseek SEEK_END on memfd failed: "
						"errno=%d (%s)\n", name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_HOLE)
			if (lseek(fds[i], (off_t)0, SEEK_HOLE) < 0) {
				if (errno != ENXIO)
					pr_fail(stderr, "%s: lseek SEEK_HOLE on memfd failed: "
						"errno=%d (%s)\n", name, errno, strerror(errno));
			}
#endif
#if defined(SEEK_DATA)
			if (lseek(fds[i], (off_t)0, SEEK_DATA) < 0) {
				if (errno != ENXIO)
					pr_fail(stderr, "%s: lseek SEEK_DATA on memfd failed: "
						"errno=%d (%s)\n", name, errno, strerror(errno));
			}
#endif
			if (!opt_do_run)
				goto clean;
		}
	
clean:
		for (i = 0; i < MAX_MEM_FDS; i++) {
			if (maps[i] != MAP_FAILED)
				(void)munmap(maps[i], size);
			if (fds[i] >= 0)
				(void)close(fds[i]);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}


/*
 *  stress_memfd()
 *	stress memfd
 */
int stress_memfd(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, nomems = 0;

	if (!set_memfd_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_memfd_bytes = MAX_MEMFD_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_memfd_bytes = MIN_MEMFD_BYTES;
	}

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
		stress_parent_died_alarm();

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
			if ((WTERMSIG(status) == SIGKILL) ||
			    (WTERMSIG(status) == SIGSEGV)) {
				log_system_mem_info();
				pr_dbg(stderr, "%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					name, instance);
				ooms++;
				goto again;
			}

		} else if (WTERMSIG(status) == SIGSEGV) {
			pr_dbg(stderr, "%s: killed by SIGSEGV, "
				"restarting again "
				"(instance %d)\n",
				name, instance);
			segvs++;
			goto again;
		}
	} else if (pid == 0) {
		setpgid(0, pgrp);

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		stress_memfd_allocs(name, counter, max_ops);
	}

	if (ooms + segvs + nomems > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			name, ooms, segvs, nomems);

	return EXIT_SUCCESS;
}

#endif
