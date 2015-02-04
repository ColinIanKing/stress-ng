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
#if defined (_POSIX_PRIORITY_SCHEDULING) || defined (__linux__)
#include <sched.h>
#endif

#include "stress-ng.h"

/* The compiler optimises out the unused cache flush and mfence calls */
#define CACHE_WRITE(flag)						\
	for (j = 0; j < MEM_CACHE_SIZE; j++) {				\
		mem_cache[i] += mem_cache[(MEM_CACHE_SIZE - 1) - i] + r;\
		if ((flag) & OPT_FLAGS_CACHE_FLUSH) {			\
			clflush(&mem_cache[i]);				\
		}							\
		if ((flag) & OPT_FLAGS_CACHE_FENCE) {			\
			mfence();					\
		}							\
		i = (i + 32769) & (MEM_CACHE_SIZE - 1);			\
		if (!opt_do_run)					\
			break;						\
	}


/*
 *  stress_cache()
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
int stress_cache(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	unsigned long total = 0;
#if defined(__linux__)
	unsigned long int cpu = 0;
	cpu_set_t mask;
#endif
	(void)instance;

	uint8_t *mem_cache = shared->mem_cache;

	do {
		uint64_t i = mwc() & (MEM_CACHE_SIZE - 1);
		uint64_t r = mwc();
		register int j;

		if ((r >> 13) & 1) {
			switch (opt_flags & OPT_FLAGS_CACHE_MASK) {
			case OPT_FLAGS_CACHE_FLUSH:
				CACHE_WRITE(OPT_FLAGS_CACHE_FLUSH);
				break;
			case OPT_FLAGS_CACHE_FENCE:
				CACHE_WRITE(OPT_FLAGS_CACHE_FENCE);
				break;
			case OPT_FLAGS_CACHE_FENCE | OPT_FLAGS_CACHE_FLUSH:
				CACHE_WRITE(OPT_FLAGS_CACHE_FLUSH | OPT_FLAGS_CACHE_FENCE);
				break;
			default:
				CACHE_WRITE(0);
				break;
			}
		} else {
			for (j = 0; j < MEM_CACHE_SIZE; j++) {
				total += mem_cache[i] + mem_cache[(MEM_CACHE_SIZE - 1) - i];
				i = (i + 32769) & (MEM_CACHE_SIZE - 1);
				if (!opt_do_run)
					break;
			}
		}
#if defined(__linux__)
		cpu++;
		cpu %= stress_get_processors_online();
		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		sched_setaffinity(0, sizeof(mask), &mask);
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_dbg(stderr, "%s: total [%lu]\n", name, total);
	return EXIT_SUCCESS;
}
