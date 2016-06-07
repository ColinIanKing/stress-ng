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

#if defined(STRESS_MREMAP)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

static size_t opt_mremap_bytes = DEFAULT_MREMAP_BYTES;
static bool set_mremap_bytes = false;

void stress_set_mremap_bytes(const char *optarg)
{
	set_mremap_bytes = true;
	opt_mremap_bytes = (size_t)get_uint64_byte(optarg);
	check_range("mmap-bytes", opt_mremap_bytes,
		MIN_MREMAP_BYTES, MAX_MREMAP_BYTES);
}

#if defined(MREMAP_FIXED)
/*
 *  rand_mremap_addr()
 *	try and find a random unmapped region of memory
 */
static inline void *rand_mremap_addr(const size_t sz, int flags)
{
	void *addr;

	flags &= ~(MREMAP_FIXED | MAP_SHARED | MAP_POPULATE);
	flags |= (MAP_PRIVATE | MAP_ANONYMOUS);

	addr = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
	if (addr == MAP_FAILED)
		return NULL;

	munmap(addr, sz);

	/*
	 * At this point, we know that we can remap to this addr
	 * in this process if we don't do any memory mappings between
	 * the munmap above and the remapping
	 */

	return addr;
}
#endif



/*
 *  try_remap()
 *	try and remap old size to new size
 */
static int try_remap(
	const char *name,
	uint8_t **buf,
	const size_t old_sz,
	const size_t new_sz)
{
	uint8_t *newbuf;
	int retry, flags = 0;
#if defined(MREMAP_MAYMOVE)
	const int maymove = MREMAP_MAYMOVE;
#else
	const int maymove = 0;
#endif

#if defined(MREMAP_FIXED) && defined(MREMAP_MAYMOVE)
	flags = maymove | (mwc32() & MREMAP_FIXED);
#else
	flags = maymove;
#endif

	for (retry = 0; retry < 100; retry++) {
#if defined(MREMAP_FIXED)
		void *addr = rand_mremap_addr(new_sz, flags);
#endif
		if (!opt_do_run)
			return 0;
#if defined(MREMAP_FIXED)
		if (addr) {
			newbuf = mremap(*buf, old_sz, new_sz, flags, addr);
		} else {
			newbuf = mremap(*buf, old_sz, new_sz, flags & ~MREMAP_FIXED);
		}
#else
		newbuf = mremap(*buf, old_sz, new_sz, flags);
#endif
		if (newbuf != MAP_FAILED) {
			*buf = newbuf;
			return 0;
		}

		switch (errno) {
		case ENOMEM:
		case EAGAIN:
			continue;
		case EINVAL:
#if defined(MREMAP_FIXED)
			/*
			 * Earlier kernels may not support this or we
			 * chose a bad random address, so just fall
			 * back to non fixed remapping
			 */
			if (flags & MREMAP_FIXED) {
				flags &= ~MREMAP_FIXED;
				continue;
			}
#endif
			break;
		case EFAULT:
		default:
			break;
		}
	}
	pr_fail(stderr, "%s: mremap failed, errno = %d (%s)\n",
		name, errno, strerror(errno));
	return -1;
}

/*
 *  stress_mremap_check()
 *	check if mmap'd data is sane
 */
static int stress_mremap_check(
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

static void stress_mremap_set(
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

static int stress_mremap_child(
	uint64_t *const counter,
	const uint64_t max_ops,
	const char *name,
	const size_t sz,
	size_t new_sz,
	const size_t page_size,
	int *flags)
{
	do {
		uint8_t *buf = NULL;
		size_t old_sz;

		if (!opt_do_run)
			break;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, *flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#ifdef MAP_POPULATE
			*flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		(void)madvise_random(buf, new_sz);
		(void)mincore_touch_pages(buf, opt_mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (opt_flags & OPT_FLAGS_VERIFY) {
			stress_mremap_set(buf, new_sz, page_size);
			if (stress_mremap_check(buf, sz, page_size) < 0) {
				pr_fail(stderr, "%s: mmap'd region of %zu "
					"bytes does not contain expected data\n",
					name, sz);
				munmap(buf, new_sz);
				return EXIT_FAILURE;
			}
		}

		old_sz = new_sz;
		new_sz >>= 1;
		while (new_sz > page_size) {
			if (try_remap(name, &buf, old_sz, new_sz) < 0) {
				munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)madvise_random(buf, new_sz);
			if (opt_flags & OPT_FLAGS_VERIFY) {
				if (stress_mremap_check(buf, new_sz, page_size) < 0) {
					pr_fail(stderr, "%s: mremap'd region "
						"of %zu bytes does "
						"not contain expected data\n",
						name, sz);
					munmap(buf, new_sz);
					return EXIT_FAILURE;
				}
			}
			old_sz = new_sz;
			new_sz >>= 1;
		}

		new_sz <<= 1;
		while (new_sz < opt_mremap_bytes) {
			if (try_remap(name, &buf, old_sz, new_sz) < 0) {
				munmap(buf, old_sz);
				return EXIT_FAILURE;
			}
			(void)madvise_random(buf, new_sz);
			old_sz = new_sz;
			new_sz <<= 1;
		}
		(void)munmap(buf, old_sz);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

/*
 *  stress_mremap()
 *	stress mmap
 */
int stress_mremap(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const size_t page_size = stress_get_pagesize();
	size_t sz, new_sz;
	int rc = EXIT_SUCCESS, flags = MAP_PRIVATE | MAP_ANONYMOUS;
	pid_t pid;
	uint32_t ooms = 0, segvs = 0, buserrs = 0;

	(void)instance;
#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	if (!set_mremap_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_mremap_bytes = MAX_MREMAP_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_mremap_bytes = MIN_MREMAP_BYTES;
	}
	new_sz = sz = opt_mremap_bytes & ~(page_size - 1);

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

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
		} else {
			rc = WEXITSTATUS(status);
		}
	} else if (pid == 0) {
		int rc;

		setpgid(0, pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(name, true);

		rc = stress_mremap_child(counter, max_ops, name, sz,
			new_sz, page_size, &flags);
		exit(rc);
	}

	if (ooms + segvs + buserrs > 0)
		pr_dbg(stderr, "%s: OOM restarts: %" PRIu32
			", SEGV restarts: %" PRIu32
			", SIGBUS signals: %" PRIu32 "\n",
			name, ooms, segvs, buserrs);

	return rc;
}

#endif
