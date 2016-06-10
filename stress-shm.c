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

#if defined(STRESS_SHM_POSIX)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#define SHM_NAME_LEN	128

typedef struct {
	int	index;
	char	shm_name[SHM_NAME_LEN];
} shm_msg_t;

static size_t opt_shm_posix_bytes = DEFAULT_SHM_POSIX_BYTES;
static size_t opt_shm_posix_objects = DEFAULT_SHM_POSIX_OBJECTS;
static bool set_shm_posix_bytes = false;
static bool set_shm_posix_objects = false;

void stress_set_shm_posix_bytes(const char *optarg)
{
	set_shm_posix_bytes = true;
	opt_shm_posix_bytes = (size_t)get_uint64_byte(optarg);
	check_range("shm-bytes", opt_shm_posix_bytes,
		MIN_SHM_POSIX_BYTES, MAX_SHM_POSIX_BYTES);
}

void stress_set_shm_posix_objects(const char *optarg)
{
	opt_shm_posix_objects = true;
	opt_shm_posix_objects = (size_t)get_uint64_byte(optarg);
	check_range("shm-segments", opt_shm_posix_objects,
		MIN_SHM_POSIX_OBJECTS, MAX_SHM_POSIX_OBJECTS);
}

/*
 *  stress_shm_posix_check()
 *	simple check if shared memory is sane
 */
static int stress_shm_posix_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	uint8_t *ptr, *end = buf + sz;
	uint8_t val;

	memset(buf, 0xa5, sz);

	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		*ptr = val;
	}

	for (val = 0, ptr = buf; ptr < end; ptr += page_size, val++) {
		if (*ptr != val)
			return -1;

	}
	return 0;
}

/*
 *  stress_shm_posix_child()
 * 	stress out the shm allocations. This can be killed by
 *	the out of memory killer, so we need to keep the parent
 *	informed of the allocated shared memory ids so these can
 *	be reaped cleanly if this process gets prematurely killed.
 */
static int stress_shm_posix_child(
	const int fd,
	uint64_t *const counter,
	const uint64_t max_ops,
	const char *name,
	size_t sz)
{
	void *addrs[MAX_SHM_POSIX_OBJECTS];
	char shm_names[MAX_SHM_POSIX_OBJECTS][SHM_NAME_LEN];
	shm_msg_t msg;
	int i;
	int rc = EXIT_SUCCESS;
	bool ok = true;
	pid_t pid = getpid();
	uint64_t id = 0;
	const size_t page_size = stress_get_pagesize();

	memset(addrs, 0, sizeof(addrs));
	memset(shm_names, 0, sizeof(shm_names));

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	do {
		for (i = 0; ok && (i < (ssize_t)opt_shm_posix_objects); i++) {
			int shm_fd;
			void *addr;
			char *shm_name = shm_names[i];

			shm_name[0] = '\0';

			if (!opt_do_run)
				goto reap;

			snprintf(shm_name, SHM_NAME_LEN,
				"/stress-ng-%u-%" PRIx64 "-%" PRIx32,
					pid, id, mwc32());

			shm_fd = shm_open(shm_name, O_CREAT | O_RDWR | O_TRUNC,
				S_IRUSR | S_IWUSR);
			if (shm_fd < 0) {
				ok = false;
				pr_fail(stderr, "%s: shm_open failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}

			/* Inform parent of the new shm name */
			msg.index = i;
			shm_name[SHM_NAME_LEN - 1] = '\0';
			strncpy(msg.shm_name, shm_name, SHM_NAME_LEN);
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_err(stderr, "%s: write failed: errno=%d: (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				(void)close(shm_fd);
				goto reap;
			}

			addr = mmap(NULL, sz, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, shm_fd, 0);
			if (addr == MAP_FAILED) {
				ok = false;
				pr_fail(stderr, "%s: mmap failed, giving up: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				(void)close(shm_fd);
				goto reap;
			}
			addrs[i] = addr;

			if (!opt_do_run) {
				(void)close(shm_fd);
				goto reap;
			}
			(void)mincore_touch_pages(addr, sz);

			if (!opt_do_run) {
				(void)close(shm_fd);
				goto reap;
			}
			(void)madvise_random(addr, sz);
#if !defined(__gnu_hurd__)
			(void)msync(addr, sz, (mwc32() & 1) ? MS_ASYNC : MS_SYNC);
#endif
			(void)fsync(shm_fd);

			/* Expand and shrink the mapping */
			(void)posix_fallocate(shm_fd, 0, sz + page_size);
			(void)posix_fallocate(shm_fd, 0, sz);
			(void)close(shm_fd);

			if (!opt_do_run)
				goto reap;
			if (stress_shm_posix_check(addr, sz, page_size) < 0) {
				ok = false;
				pr_fail(stderr, "%s: memory check failed\n", name);
				rc = EXIT_FAILURE;
				goto reap;
			}
			id++;
			(*counter)++;
		}
reap:
		for (i = 0; ok && (i < (ssize_t)opt_shm_posix_objects); i++) {
			char *shm_name = shm_names[i];

			if (addrs[i])
				(void)munmap(addrs[i], sz);
			if (*shm_name) {
				if (shm_unlink(shm_name) < 0) {
					pr_fail(stderr, "%s: shm_unlink "
						"failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
			}

			/* Inform parent shm ID is now free */
			msg.index = i;
			msg.shm_name[SHM_NAME_LEN - 1] = '\0';
			strncpy(msg.shm_name, shm_name, SHM_NAME_LEN - 1);
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_dbg(stderr, "%s: write failed: errno=%d: (%s)\n",
					name, errno, strerror(errno));
				ok = false;
			}
			addrs[i] = NULL;
			*shm_name = '\0';
		}
	} while (ok && opt_do_run && (!max_ops || *counter < max_ops));

	/* Inform parent of end of run */
	msg.index = -1;
	strncpy(msg.shm_name, "", SHM_NAME_LEN);
	if (write(fd, &msg, sizeof(msg)) < 0) {
		pr_err(stderr, "%s: write failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	}

	return rc;
}

/*
 *  stress_shm()
 *	stress SYSTEM V shared memory
 */
int stress_shm(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	size_t orig_sz, sz;
	int pipefds[2];
	int rc = EXIT_SUCCESS;
	ssize_t i;
	pid_t pid;
	bool retry = true;
	uint32_t restarts = 0;

	if (!set_shm_posix_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_shm_posix_bytes = MAX_SHM_POSIX_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_shm_posix_bytes = MIN_SHM_POSIX_BYTES;
	}
	if (!set_shm_posix_objects) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_shm_posix_objects = MAX_SHM_POSIX_OBJECTS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_shm_posix_objects = MIN_SHM_POSIX_OBJECTS;
	}
	orig_sz = sz = opt_shm_posix_bytes & ~(page_size - 1);

	while (opt_do_run && retry) {
		if (pipe(pipefds) < 0) {
			pr_fail_dbg(name, "pipe");
			return EXIT_FAILURE;
		}
fork_again:
		pid = fork();
		if (pid < 0) {
			/* Can't fork, retry? */
			if (errno == EAGAIN)
				goto fork_again;
			pr_err(stderr, "%s: fork failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			(void)close(pipefds[0]);
			(void)close(pipefds[1]);

			/* Nope, give up! */
			return EXIT_FAILURE;
		} else if (pid > 0) {
			/* Parent */
			int status;
			char shm_names[MAX_SHM_POSIX_OBJECTS][SHM_NAME_LEN];

			setpgid(pid, pgrp);
			(void)close(pipefds[1]);

			memset(shm_names, 0, sizeof(shm_names));

			while (opt_do_run) {
				ssize_t n;
				shm_msg_t 	msg;
				char *shm_name;

				/*
				 *  Blocking read on child shm ID info
				 *  pipe.  We break out if pipe breaks
				 *  on child death, or child tells us
				 *  off its demise.
				 */
				n = read(pipefds[0], &msg, sizeof(msg));
				if (n <= 0) {
					if ((errno == EAGAIN) || (errno == EINTR))
						continue;
					if (errno) {
						pr_fail_dbg(name, "read");
						break;
					}
					pr_fail_dbg(name, "zero byte read");
					break;
				}
				if ((msg.index < 0) ||
				    (msg.index >= MAX_SHM_POSIX_OBJECTS)) {
					retry = false;
					break;
				}

				shm_name = shm_names[msg.index];
				shm_name[SHM_NAME_LEN - 1] = '\0';
				strncpy(shm_name, msg.shm_name, SHM_NAME_LEN - 1);
			}
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGKILL)) {
					log_system_mem_info();
					pr_dbg(stderr, "%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						name, instance);
					restarts++;
				}
			}
			(void)close(pipefds[1]);

			/*
			 *  The child may have been killed by the OOM killer or
			 *  some other way, so it may have left the shared
			 *  memory segment around.  At this point the child
			 *  has died, so we should be able to remove the
			 *  shared memory segment.
			 */
			for (i = 0; i < (ssize_t)opt_shm_posix_objects; i++) {
				char *shm_name = shm_names[i];
				if (*shm_name)
					(void)shm_unlink(shm_name);
			}
		} else if (pid == 0) {
			/* Child, stress memory */
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			(void)close(pipefds[0]);
			rc = stress_shm_posix_child(pipefds[1], counter,
				max_ops, name, sz);
			(void)close(pipefds[1]);
			_exit(rc);
		}
	}
	if (orig_sz != sz)
		pr_dbg(stderr, "%s: reduced shared memory size from "
			"%zu to %zu bytes\n", name, orig_sz, sz);
	if (restarts) {
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32 "\n",
			name, restarts);
	}
	return rc;
}

#endif
