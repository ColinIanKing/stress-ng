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
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>

#include "stress-ng.h"

#define KEY_GET_RETRIES		(40)

/*
 *  Note, running this test with the --maximize option on
 *  low memory systems with many instances can trigger the
 *  OOM killer fairly easily.  The test tries hard to reap
 *  reap shared memory segments that are left over if the
 *  child is killed, however if the OOM killer kills the
 *  parent that does the reaping, then one can be left with
 *  a system with many shared segments still reserved and
 *  little free memory.
 */
typedef struct {
	int	index;
	int	shm_id;
} shm_msg_t;

static size_t opt_shm_sysv_bytes = DEFAULT_SHM_SYSV_BYTES;
static size_t opt_shm_sysv_segments = DEFAULT_SHM_SYSV_SEGMENTS;
static bool set_shm_sysv_bytes = false;
static bool set_shm_sysv_segments = false;

static int shm_flags[] = {
#if defined(SHM_HUGETLB)
	SHM_HUGETLB,
#endif
#if defined(SHM_HUGE_2MB)
	SHM_HUGE_2MB,
#endif
#if defined(SHM_HUGE_1GB)
	SHM_HUGE_1GB,
#endif
/* This will segv if no backing, so don't use it for now */
/*
#if defined(SHM_NO_RESERVE)
	SHM_NO_RESERVE
#endif
*/
	0
};

void stress_set_shm_sysv_bytes(const char *optarg)
{
	set_shm_sysv_bytes = true;
	opt_shm_sysv_bytes = (size_t)get_uint64_byte(optarg);
	check_range("shm-sysv-bytes", opt_shm_sysv_bytes,
		MIN_SHM_SYSV_BYTES, MAX_SHM_SYSV_BYTES);
}

void stress_set_shm_sysv_segments(const char *optarg)
{
	opt_shm_sysv_segments = true;
	opt_shm_sysv_segments = (size_t)get_uint64_byte(optarg);
	check_range("shm-sysv-segments", opt_shm_sysv_segments,
		MIN_SHM_SYSV_SEGMENTS, MAX_SHM_SYSV_SEGMENTS);
}

/*
 *  stress_shm_sysv_check()
 *	simple check if shared memory is sane
 */
static int stress_shm_sysv_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	uint8_t *ptr, *end = buf + sz;
	uint8_t val;

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
 *  handle_shm_sysv_sigalrm()
 *      catch SIGALRM, flag termination
 */
static MLOCKED void handle_shm_sysv_sigalrm(int dummy)
{
	(void)dummy;

	opt_do_run = false;
}

/*
 *  stress_shm_sysv_child()
 * 	stress out the shm allocations. This can be killed by
 *	the out of memory killer, so we need to keep the parent
 *	informed of the allocated shared memory ids so these can
 *	be reaped cleanly if this process gets prematurely killed.
 */
static int stress_shm_sysv_child(
	const int fd,
	uint64_t *const counter,
	const uint64_t max_ops,
	const char *name,
	const size_t max_sz,
	const size_t page_size)
{
	void *addrs[MAX_SHM_SYSV_SEGMENTS];
	key_t keys[MAX_SHM_SYSV_SEGMENTS];
	int shm_ids[MAX_SHM_SYSV_SEGMENTS];
	shm_msg_t msg;
	size_t i;
	int rc = EXIT_SUCCESS;
	bool ok = true;
	int mask = ~0;
	int32_t instances;

	if (stress_sighandler(name, SIGALRM, handle_shm_sysv_sigalrm, NULL) < 0)
		return EXIT_FAILURE;

	memset(addrs, 0, sizeof(addrs));
	memset(keys, 0, sizeof(keys));
	for (i = 0; i < MAX_SHM_SYSV_SEGMENTS; i++)
		shm_ids[i] = -1;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	if ((instances = stressor_instances(STRESS_SHM_SYSV)) < 1)
		instances = stress_get_processors_configured();
	/* Should never happen, but be safe */
	if (instances < 1)
		instances = 1;

	do {
		size_t sz = max_sz;

		for (i = 0; i < opt_shm_sysv_segments; i++) {
			int shm_id, count = 0;
			void *addr;
			key_t key;
			size_t shmall, freemem, totalmem;

			/* Try hard not to overcommit at this current time */
			stress_get_memlimits(&shmall, &freemem, &totalmem);
			shmall /= instances;
			freemem /= instances;
			if ((shmall > page_size) && sz > shmall)
				sz = shmall;
			if ((freemem > page_size) && sz > freemem)
				sz = freemem;
			if (!opt_do_run)
				goto reap;

			for (count = 0; count < KEY_GET_RETRIES; count++) {
				bool unique = true;
				const int rnd =
					mwc32() % SIZEOF_ARRAY(shm_flags);
				const int rnd_flag = shm_flags[rnd] & mask;

				if (sz < page_size)
					goto reap;

				/* Get a unique key */
				do {
					size_t j;

					if (!opt_do_run)
						goto reap;

					/* Get a unique random key */
					key = (key_t)mwc16();
					for (j = 0; j < i; j++) {
						if (key == keys[j]) {
							unique = false;
							break;
						}
					}
					if (!opt_do_run)
						goto reap;

				} while (!unique);

				shm_id = shmget(key, sz,
					IPC_CREAT | IPC_EXCL |
					S_IRUSR | S_IWUSR | rnd_flag);
				if (shm_id >= 0)
					break;
				if (errno == EINTR)
					goto reap;
				if (errno == EPERM) {
					/* ignore using the flag again */
					mask &= ~rnd_flag;
				}
				if ((errno == EINVAL) || (errno == ENOMEM)) {
					/*
					 * On some systems we may need
					 * to reduce the size
					 */
					sz = sz / 2;
				}
			}
			if (shm_id < 0) {
				ok = false;
				pr_fail(stderr, "%s: shmget failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}

			/* Inform parent of the new shm ID */
			msg.index = i;
			msg.shm_id = shm_id;
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_err(stderr, "%s: write failed: errno=%d: (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}

			addr = shmat(shm_id, NULL, 0);
			if (addr == (char *) -1) {
				ok = false;
				pr_fail(stderr, "%s: shmat failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto reap;
			}
			addrs[i] = addr;
			shm_ids[i] = shm_id;
			keys[i] = key;

			if (!opt_do_run)
				goto reap;
			(void)mincore_touch_pages(addr, sz);
#if !defined(__gnu_hurd__)
			(void)msync(addr, sz, (mwc32() & 1) ? MS_ASYNC : MS_SYNC);
#endif

			if (!opt_do_run)
				goto reap;
			(void)madvise_random(addr, sz);

			if (!opt_do_run)
				goto reap;
			if (stress_shm_sysv_check(addr, sz, page_size) < 0) {
				ok = false;
				pr_fail(stderr, "%s: memory check failed\n", name);
				rc = EXIT_FAILURE;
				goto reap;
			}
#if defined(IPC_STAT)
			{
				struct shmid_ds ds;

				if (shmctl(shm_id, IPC_STAT, &ds) < 0)
					pr_fail_dbg(name, "shmctl IPC_STAT");
			}
#endif
#if defined(__linux__) && defined(IPC_INFO)
			{
				struct shminfo s;

				if (shmctl(shm_id, IPC_INFO, (struct shmid_ds *)&s) < 0)
					pr_fail_dbg(name, "semctl IPC_INFO");
			}
#endif
#if defined(__linux__) && defined(SHM_INFO)
			{
				struct shm_info s;

				if (shmctl(shm_id, SHM_INFO, (struct shmid_ds *)&s) < 0)
					pr_fail_dbg(name, "semctl SHM_INFO");
			}
#endif
			(*counter)++;
		}
reap:
		for (i = 0; i < opt_shm_sysv_segments; i++) {
			if (addrs[i]) {
				if (shmdt(addrs[i]) < 0) {
					pr_fail(stderr, "%s: shmdt failed: errno=%d (%s)\n",
						name, errno, strerror(errno));
				}
			}
			if (shm_ids[i] >= 0) {
				if (shmctl(shm_ids[i], IPC_RMID, NULL) < 0) {
					if (errno != EIDRM)
						pr_fail(stderr, "%s: shmctl failed: errno=%d (%s)\n",
							name, errno, strerror(errno));
				}
			}

			/* Inform parent shm ID is now free */
			msg.index = i;
			msg.shm_id = -1;
			if (write(fd, &msg, sizeof(msg)) < 0) {
				pr_dbg(stderr, "%s: write failed: errno=%d: (%s)\n",
					name, errno, strerror(errno));
				ok = false;
			}
			addrs[i] = NULL;
			shm_ids[i] = -1;
			keys[i] = 0;
		}
	} while (ok && opt_do_run && (!max_ops || *counter < max_ops));

	/* Inform parent of end of run */
	msg.index = -1;
	msg.shm_id = -1;
	if (write(fd, &msg, sizeof(msg)) < 0) {
		pr_err(stderr, "%s: write failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		rc = EXIT_FAILURE;
	}

	return rc;
}

/*
 *  stress_shm_sysv()
 *	stress SYSTEM V shared memory
 */
int stress_shm_sysv(
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

	if (!set_shm_sysv_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_shm_sysv_bytes = MAX_SHM_SYSV_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_shm_sysv_bytes = MIN_SHM_SYSV_BYTES;
	}

	if (!set_shm_sysv_segments) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_shm_sysv_segments = MAX_SHM_SYSV_SEGMENTS;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_shm_sysv_segments = MIN_SHM_SYSV_SEGMENTS;
	}
	orig_sz = sz = opt_shm_sysv_bytes & ~(page_size - 1);

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
			int status, shm_ids[MAX_SHM_SYSV_SEGMENTS];

			setpgid(pid, pgrp);
			set_oom_adjustment(name, false);
			(void)close(pipefds[1]);

			for (i = 0; i < (ssize_t)MAX_SHM_SYSV_SEGMENTS; i++)
				shm_ids[i] = -1;

			while (opt_do_run) {
				shm_msg_t 	msg;
				ssize_t n;

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
				    (msg.index >= MAX_SHM_SYSV_SEGMENTS)) {
					retry = false;
					break;
				}
				shm_ids[msg.index] = msg.shm_id;
			}
			(void)kill(pid, SIGALRM);
			(void)waitpid(pid, &status, 0);
			if (WIFSIGNALED(status)) {
				if ((WTERMSIG(status) == SIGKILL) ||
				    (WTERMSIG(status) == SIGBUS)) {
					log_system_mem_info();
					pr_dbg(stderr, "%s: assuming killed by OOM killer, "
						"restarting again (instance %d)\n",
						name, instance);
					restarts++;
				}
			}
			(void)close(pipefds[0]);
			/*
			 *  The child may have been killed by the OOM killer or
			 *  some other way, so it may have left the shared
			 *  memory segment around.  At this point the child
			 *  has died, so we should be able to remove the
			 *  shared memory segment.
			 */
			for (i = 0; i < (ssize_t)opt_shm_sysv_segments; i++) {
				if (shm_ids[i] != -1)
					(void)shmctl(shm_ids[i], IPC_RMID, NULL);
			}
		} else if (pid == 0) {
			/* Child, stress memory */
			setpgid(0, pgrp);
			stress_parent_died_alarm();

			/*
			 * Nicing the child may OOM it first as this
			 * doubles the OOM score
			 */
			if (nice(5) < 0)
				pr_dbg(stderr, "%s: nice of child failed, "
					"(instance %d)\n", name, instance);

			(void)close(pipefds[0]);
			rc = stress_shm_sysv_child(pipefds[1], counter,
				max_ops, name, sz, page_size);
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
