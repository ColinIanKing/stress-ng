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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "stress-ng.h"

static size_t opt_mmap_bytes = DEFAULT_MMAP_BYTES;

void stress_set_mmap_bytes(const char *optarg)
{
	opt_mmap_bytes = (size_t)get_uint64_byte(optarg);
	check_range("mmap-bytes", opt_mmap_bytes,
		MIN_MMAP_BYTES, MAX_MMAP_BYTES);
}

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
static int stress_mmap_check(uint8_t *buf, const size_t sz)
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

static void stress_mmap_set(uint8_t *buf, const size_t sz)
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
 *  stress_mmap_mprotect()
 *	cycle through page settings on a region of mmap'd memory
 */
static void stress_mmap_mprotect(void *addr, size_t len)
{
	if (opt_flags & OPT_FLAGS_MMAP_MPROTECT) {
		/* Cycle through potection */
		if (mprotect(addr, len, PROT_NONE) < 0)
			pr_fail(stderr, "mprotect set to PROT_NONE failed\n");
		if (mprotect(addr, len, PROT_READ) < 0)
			pr_fail(stderr, "mprotect set to PROT_READ failed\n");
		if (mprotect(addr, len, PROT_WRITE) < 0)
			pr_fail(stderr, "mprotect set to PROT_WRITE failed\n");
		if (mprotect(addr, len, PROT_EXEC) < 0)
			pr_fail(stderr, "mprotect set to PROT_EXEC failed\n");
		if (mprotect(addr, len, PROT_READ | PROT_WRITE) < 0)
			pr_fail(stderr, "mprotect set to PROT_READ | PROT_WRITE failed\n");
	}
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
	uint8_t *buf = NULL;
	const size_t page_size = stress_get_pagesize();
	const size_t sz = opt_mmap_bytes & ~(page_size - 1);
	const size_t pages4k = sz / page_size;
#if !defined(__gnu_hurd__)
	const int ms_flags = (opt_flags & OPT_FLAGS_MMAP_ASYNC) ?
		MS_ASYNC : MS_SYNC;
#endif
	const pid_t pid = getpid();
	int fd = -1, flags = MAP_PRIVATE | MAP_ANONYMOUS;
	char filename[PATH_MAX];

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	(void)instance;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	if (opt_flags & OPT_FLAGS_MMAP_FILE) {
		char ch = '\0';

		if (stress_temp_dir_mk(name, pid, instance) < 0)
			return EXIT_FAILURE;

		(void)stress_temp_filename(filename, sizeof(filename),
			name, pid, instance, mwc());

		(void)umask(0077);
		if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
			pr_failed_err(name, "open");
			(void)unlink(filename);
			(void)stress_temp_dir_rm(name, pid, instance);

			return EXIT_FAILURE;
		}
		(void)unlink(filename);
		if (lseek(fd, sz - sizeof(ch), SEEK_SET) < 0) {
			pr_failed_err(name, "lseek");
			(void)close(fd);
			(void)stress_temp_dir_rm(name, pid, instance);

			return EXIT_FAILURE;
		}
		if (write(fd, &ch, sizeof(ch)) != sizeof(ch)) {
			pr_failed_err(name, "write");
			(void)close(fd);
			(void)stress_temp_dir_rm(name, pid, instance);

			return EXIT_FAILURE;
		}
		flags &= ~(MAP_ANONYMOUS | MAP_PRIVATE);
		flags |= MAP_SHARED;
	}

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;

		if (!opt_do_run)
			break;
		buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, fd, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
#ifdef MAP_POPULATE
			flags &= ~MAP_POPULATE;
#endif
			continue;	/* Try again */
		}
		if (opt_flags & OPT_FLAGS_MMAP_FILE) {
			memset(buf, 0xff, sz);
#if !defined(__gnu_hurd__)
			(void)msync(buf, sz, ms_flags);
#endif
		}
		(void)madvise_random(buf, sz);
		(void)mincore_touch_pages(buf, opt_mmap_bytes);
		stress_mmap_mprotect(buf, sz);
		memset(mapped, PAGE_MAPPED, sizeof(mapped));
		for (n = 0; n < pages4k; n++)
			mappings[n] = buf + (n * page_size);

		/* Ensure we can write to the mapped pages */
		stress_mmap_set(buf, sz);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			if (stress_mmap_check(buf, sz) < 0)
				pr_fail(stderr, "mmap'd region of %zu bytes does "
					"not contain expected data\n", sz);
		}

		/*
		 *  Step #1, unmap all pages in random order
		 */
		(void)mincore_touch_pages(buf, opt_mmap_bytes);
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
					(void)madvise_random(mappings[page], page_size);
					stress_mmap_mprotect(mappings[page], page_size);
					munmap(mappings[page], page_size);
					n--;
					break;
				}
				if (!opt_do_run)
					goto cleanup;
			}
		}

#ifdef MAP_FIXED
		/*
		 *  Step #2, map them back in random order
		 */
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc() % pages4k;
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
					mappings[page] = mmap(mappings[page], page_size, PROT_READ | PROT_WRITE, MAP_FIXED | flags, fd, offset);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						(void)mincore_touch_pages(buf, page_size);
						(void)madvise_random(mappings[page], page_size);
						stress_mmap_mprotect(mappings[page], page_size);
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size);
						if (stress_mmap_check(mappings[page], page_size) < 0)
							pr_fail(stderr, "mmap'd region of %zu bytes does "
								"not contain expected data\n", page_size);
						if (opt_flags & OPT_FLAGS_MMAP_FILE) {
							memset(mappings[page], n, page_size);
#if !defined(__gnu_hurd__)
							(void)msync(mappings[page], page_size, ms_flags);
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
				stress_mmap_mprotect(mappings[n], page_size);
				munmap(mappings[n], page_size);
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (opt_flags & OPT_FLAGS_MMAP_FILE) {
		(void)close(fd);
		(void)stress_temp_dir_rm(name, pid, instance);
	}
	return EXIT_SUCCESS;
}
