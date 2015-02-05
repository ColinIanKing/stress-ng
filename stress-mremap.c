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

#if defined(__linux__)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

static size_t opt_mremap_bytes = DEFAULT_MMAP_BYTES;

void stress_set_mremap_bytes(const char *optarg)
{
	opt_mremap_bytes = (size_t)get_uint64_byte(optarg);
	check_range("mmap-bytes", opt_mremap_bytes,
		MIN_MMAP_BYTES, MAX_MMAP_BYTES);
}


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
	int retry;
#if defined(MREMAP_MAYMOVE)
	int flags = MREMAP_MAYMOVE;
#else
	int flags = 0;
#endif

	for (retry = 0; retry < 100; retry++) {
		if (!opt_do_run)
			return 0;
		newbuf = mremap(*buf, old_sz, new_sz, flags);
		if (newbuf != MAP_FAILED) {
			*buf = newbuf;
			return 0;
		}

		switch (errno) {
		case ENOMEM:
		case EAGAIN:
			continue;
		case EFAULT:
		case EINVAL:
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
static int stress_mremap_check(uint8_t *buf, const size_t sz)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += 4096) {
		if (!opt_do_run)
			break;
		for (j = 0; j < 4096; j++)
			if (*ptr++ != val++)
				return -1;
		val++;
	}
	return 0;
}

static void stress_mremap_set(uint8_t *buf, const size_t sz)
{
	size_t i, j;
	uint8_t val = 0;
	uint8_t *ptr = buf;

	for (i = 0; i < sz; i += 4096) {
		if (!opt_do_run)
			break;
		for (j = 0; j < 4096; j++)
			*ptr++ = val++;
		val++;
	}
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
	uint8_t *buf = NULL;
	const size_t page_size = stress_get_pagesize();
	const size_t sz = opt_mremap_bytes & ~(page_size - 1);
	size_t new_sz = sz, old_sz;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	(void)instance;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	do {

		if (!opt_do_run)
			break;

		buf = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#ifdef MAP_POPULATE
			flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		(void)madvise_random(buf, new_sz);
		(void)mincore_touch_pages(buf, opt_mremap_bytes);

		/* Ensure we can write to the mapped pages */
		if (opt_flags & OPT_FLAGS_VERIFY) {
			stress_mremap_set(buf, new_sz);
			if (stress_mremap_check(buf, sz) < 0) {
				pr_fail(stderr, "mmap'd region of %zu bytes does "
					"not contain expected data\n", sz);
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
				if (stress_mremap_check(buf, new_sz) < 0) {
					pr_fail(stderr, "mremap'd region of %zu bytes does "
						"not contain expected data\n", sz);
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

#endif
