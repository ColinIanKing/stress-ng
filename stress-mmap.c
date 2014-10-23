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
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include "stress-ng.h"

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
int stress_mmap_check(uint8_t *buf, const size_t sz)
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

void stress_mmap_set(uint8_t *buf, const size_t sz)
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
#ifdef _SC_PAGESIZE
	const long page_size = sysconf(_SC_PAGESIZE);
#else
	const long page_size = PAGE_4K;
#endif
	const size_t sz = opt_mmap_bytes & ~(page_size - 1);
	const size_t pages4k = sz / page_size;
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;

#ifdef MAP_POPULATE
	flags |= MAP_POPULATE;
#endif
	(void)instance;

	/* Make sure this is killable by OOM killer */
	set_oom_adjustment(name, true);

	do {
		uint8_t mapped[pages4k];
		uint8_t *mappings[pages4k];
		size_t n;

		if (!opt_do_run)
			break;
		buf = mmap(NULL, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED) {
			/* Force MAP_POPULATE off, just in case */
			flags &= ~MAP_POPULATE;
			continue;	/* Try again */
		}
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
		for (n = pages4k; n; ) {
			uint64_t j, i = mwc() % pages4k;
			for (j = 0; j < n; j++) {
				uint64_t page = (i + j) % pages4k;
				if (mapped[page] == PAGE_MAPPED) {
					mapped[page] = 0;
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
					/*
					 * Attempt to map them back into the original address, this
					 * may fail (it's not the most portable operation), so keep
					 * track of failed mappings too
					 */
					mappings[page] = mmap(mappings[page], page_size, PROT_READ | PROT_WRITE, MAP_FIXED | flags, -1, 0);
					if (mappings[page] == MAP_FAILED) {
						mapped[page] = PAGE_MAPPED_FAIL;
						mappings[page] = NULL;
					} else {
						mapped[page] = PAGE_MAPPED;
						/* Ensure we can write to the mapped page */
						stress_mmap_set(mappings[page], page_size);
						if (stress_mmap_check(mappings[page], page_size) < 0)
							pr_fail(stderr, "mmap'd region of %lu bytes does "
								"not contain expected data\n", page_size);
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
			if (mapped[n] & PAGE_MAPPED)
				munmap(mappings[n], page_size);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
