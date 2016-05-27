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
#include <signal.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

#define NO_MEM_RETRIES_MAX	(256)

static size_t opt_mmap_bytes = DEFAULT_MMAP_BYTES;
static bool set_mmap_bytes = false;

/* Misc randomly chosen mmap flags */
static int mmap_flags[] = {
#if defined(MAP_HUGE_2MB) && defined(MAP_HUGETLB)
	MAP_HUGE_2MB | MAP_HUGETLB,
#endif
#if defined(MAP_HUGE_1GB) && defined(MAP_HUGETLB)
	MAP_HUGE_1GB | MAP_HUGETLB,
#endif
#if defined(MAP_NONBLOCK)
	MAP_NONBLOCK,
#endif
#if defined(MAP_GROWSDOWN)
	MAP_GROWSDOWN,
#endif
#if defined(MAP_LOCKED)
	MAP_LOCKED,
#endif
#if defined(MAP_32BIT) && (defined(__x86_64__) || defined(__x86_64))
	MAP_32BIT,
#endif
/* This will segv if no backing, so don't use it for now */
#if 0 && defined(MAP_NORESERVE)
	MAP_NORESERVE,
#endif
	0
};

void stress_set_mmap_bytes(const char *optarg)
{
	set_mmap_bytes = true;
	opt_mmap_bytes = (size_t)get_uint64_byte(optarg);
	check_range("mmap-bytes", opt_mmap_bytes,
		MIN_MMAP_BYTES, MAX_MMAP_BYTES);
}

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
static int stress_mmap_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += page_size) {
		if (!opt_do_run)
			break;
		for (j = 0; j < page_size; j++)
			if (*ptr++ != val++)
				return -1;
		val++;
	}
	return 0;
}

static void stress_mmap_set(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += page_size) {
		if (!opt_do_run)
			break;
		for (j = 0; j < page_size; j++)
			*ptr++ = val++;
		val++;
	}
}

/*
 *  stress_mmap_mprotect()
 *	cycle through page settings on a region of mmap'd memory
 */
static void stress_mmap_mprotect(const char *name, void *addr, const size_t len)
{
	if (opt_flags & OPT_FLAGS_MMAP_MPROTECT) {
		/* Cycle through potection */
		if (mprotect(addr, len, PROT_NONE) < 0)
			pr_fail(stderr, "%s: mprotect set to PROT_NONE failed\n", name);
		if (mprotect(addr, len, PROT_READ) < 0)
			pr_fail(stderr, "%s: mprotect set to PROT_READ failed\n", name);
		if (mprotect(addr, len, PROT_WRITE) < 0)
			pr_fail(stderr, "%s: mprotect set to PROT_WRITE failed\n", name);
		if (mprotect(addr, len, PROT_EXEC) < 0)
			pr_fail(stderr, "%s: mprotect set to PROT_EXEC failed\n", name);
		if (mprotect(addr, len, PROT_READ | PROT_WRITE) < 0)
			pr_fail(stderr, "%s: mprotect set to PROT_READ | PROT_WRITE failed\n", name);
	}
}

static void stress_mmap_child(
	uint64_t *const counter,
	const uint64_t max_ops,
	const char *name,
	const int fd,
	int *flags,
	const size_t page_size,
	const size_t sz,
	const size_t pages4k)
{
	int no_mem_retries = 0;
#if !defined(__gnu_hurd__)
	const int ms_flags = (opt_flags & OPT_FLAGS_MMAP_ASYNC) ?
		MS_ASYNC : MS_SYNC;
#endif

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;
		const int rnd = mwc32() % SIZEOF_ARRAY(mmap_flags);
		const int rnd_flag = mmap_flags[rnd];
		uint8_t *buf = NULL;

		if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
			pr_err(stderr, "%s: gave up trying to mmap, no available memory\n",
				name);
			break;
		}

		if (!opt_do_run)
			break;
		buf = (uint8_t *)mmap(NULL, sz,
			PROT_READ | PROT_WRITE, *flags | rnd_flag, fd, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#ifdef MAP_POPULATE
			*flags &= ~MAP_POPULATE;
#endif
			no_mem_retries++;
			if (no_mem_retries > 1)
				usleep(100000);
			continue;	/* Try again */
		}
		if (opt_flags & OPT_FLAGS_MMAP_FILE) {
			memset(buf, 0xff, sz);
#if !defined(__gnu_hurd__)
			(void)msync((void *)buf, sz, ms_flags);
#endif
		}
		(void)madvise_random(buf, sz);
		(void)mincore_touch_pages(buf, opt_mmap_bytes);
		stress_mmap_mprotect(name, buf, sz);
		memset(mapped, PAGE_MAPPED, sizeof(mapped));
		for (n = 0; n < pages4k; n++)
			mappings[n] = buf + (n * page_size);

		/* Ensure we can write to the mapped pages */
		stress_mmap_set(buf, sz, page_size);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			if (stress_mmap_check(buf, sz, page_size) < 0)
				pr_fail(stderr, "%s: mmap'd region of %zu bytes does "
					"not contain expected data\n", name, sz);
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)mincore_touch_pages(buf, opt_mmap_bytes);
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc64() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					(void)madvise_random(mappings[page], page_size);
					stress_mmap_mprotect(name, mappings[page], page_size);
					(void)munmap((void *)mappings[page], page_size);
					n--;
					break;
				}
				if (!opt_do_run)
					goto cleanup;
			}
		}
		(void)munmap((void *)buf, sz);
#ifdef MAP_FIXED
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc64() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (!mapped[page]) {
					off_t offset = (opt_flags & OPT_FLAGS_MMAP_FILE) ?
							page * page_size : 0;
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page] = (uint8_t *)mmap((void *)mappings[page],
						page_size, PROT_READ | PROT_WRITE, MAP_FIXED | *flags, fd, offset);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)mincore_touch_pages(mappings[page], page_size);
						(void)madvise_random(mappings[page], page_size);
						stress_mmap_mprotect(name, mappings[page], page_size);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size, page_size);
						if (stress_mmap_check(mappings[page], page_size, page_size) < 0)
							pr_fail(stderr, "%s: mmap'd region of %zu bytes does "
								"not contain expected data\n", name, page_size);
						if (opt_flags & OPT_FLAGS_MMAP_FILE) {
							memset(mappings[page], n, page_size);
#if !defined(__gnu_hurd__)
							(void)msync((void *)mappings[page], page_size, ms_flags);
#endif
						}
					}
					n--;
					break;
				}
				if (!opt_do_run)
					goto cleanup;
			}
		}
#endif
cleanup:
		/*
		 *  Step #3, unmap them all
		 */
		for (n = 0; n < pages4k; n++) {
			if (mapped[n] & PAGE_MAPPED) {
				(void)madvise_random(mappings[n], page_size);
				stress_mmap_mprotect(name, mappings[n], page_size);
				(void)munmap((void *)mappings[n], page_size);
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
}

/*
 *  stress_mmap()
 *	stress mmap
 */
int stress_mmap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	size_t sz, pages4k;
	const pid_t mypid = getpid();
	pid_t pid;
	int fd = -1, flags = MAP_PRIVATE | MAP_ANONYMOUS;
	uint32_t ooms = 0, segvs = 0, buserrs = 0;
	char filename[PATH_MAX];

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	if (!set_mmap_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_mmap_bytes = MAX_MMAP_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_mmap_bytes = MIN_MMAP_BYTES;
	}
	sz = opt_mmap_bytes & ~(page_size - 1);
	pages4k = sz / page_size;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	if (opt_flags & OPT_FLAGS_MMAP_FILE) {
		ssize_t ret, rc;
		char ch = '\0';

		rc = stress_temp_dir_mk(name, mypid, instance);
		if (rc < 0)
			return exit_status(-rc);

		(void)stress_temp_filename(filename, sizeof(filename),
			name, mypid, instance, mwc32());

		(void)umask(0077);
		if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
			rc = exit_status(errno);
			pr_fail_err(name, "open");
			(void)unlink(filename);
			(void)stress_temp_dir_rm(name, mypid, instance);

			return rc;
		}
		(void)unlink(filename);
		if (lseek(fd, sz - sizeof(ch), SEEK_SET) < 0) {
			pr_fail_err(name, "lseek");
			(void)close(fd);
			(void)stress_temp_dir_rm(name, mypid, instance);

			return EXIT_FAILURE;
		}
redo:
		ret = write(fd, &ch, sizeof(ch));
		if (ret != sizeof(ch)) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo;
			rc = exit_status(errno);
			pr_fail_err(name, "write");
			(void)close(fd);
			(void)stress_temp_dir_rm(name, mypid, instance);

			return rc;
		}
		flags &= ~(MAP_ANONYMOUS | MAP_PRIVATE);
		flags |= MAP_SHARED;
	}

again:
	if (!opt_do_run)
		goto cleanup;
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
			/* If we got killed by sigbus, re-start */
			if (WTERMSIG(status) == SIGBUS) {
				/* Happens frequently, so be silent */
				buserrs++;
				goto again;
			}

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
		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		stress_mmap_child(counter, max_ops, name, fd, &flags, page_size, sz, pages4k);
	}

cleanup:
	if (opt_flags & OPT_FLAGS_MMAP_FILE) {
		(void)close(fd);
		(void)stress_temp_dir_rm(name, mypid, instance);
	}
	if (ooms + segvs + buserrs > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", SIGBUS signals: %" PRIu32 "\n",
			name, ooms, segvs, buserrs);

	return EXIT_SUCCESS;
}
