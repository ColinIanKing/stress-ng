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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#if defined(__linux__)
#include <sys/syscall.h>
#ifndef ICACHE
#define	ICACHE	(1 << 0)
#endif
#ifndef DCACHE
#define DCACHE	(1 << 1)
#endif
#if defined(_POSIX_PRIORITY_SCHEDULING)
#include <sched.h>
#endif
#endif

#if defined(__linux__) && defined(__NR_cacheflush)
static inline int sys_cacheflush(char *addr, int nbytes, int cache)
{
	return (int)syscall(__NR_cacheflush, addr, nbytes, cache);
}
#endif

/* The compiler optimises out the unused cache flush and mfence calls */
#define CACHE_WRITE(flag)						\
	for (j = 0; j < mem_cache_size; j++) {				\
		if ((flag) & OPT_FLAGS_CACHE_PREFETCH) {		\
			__builtin_prefetch(&mem_cache[i + 1], 1, 1);	\
		}							\
		mem_cache[i] += mem_cache[(mem_cache_size - 1) - i] + r;\
		if ((flag) & OPT_FLAGS_CACHE_FLUSH) {			\
			clflush(&mem_cache[i]);				\
		}							\
		if ((flag) & OPT_FLAGS_CACHE_FENCE) {			\
			mfence();					\
		}							\
		i = (i + 32769) & (mem_cache_size - 1);			\
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
#if defined(__linux__)
	cpu_set_t mask;
	uint32_t cpu = 0;
	const uint32_t cpus = stress_get_processors_configured();
	cpu_set_t proc_mask;
	bool pinned = false;
#endif
	uint32_t total = 0;
	int ret = EXIT_SUCCESS;
	uint8_t *const mem_cache = shared->mem_cache;
	const uint64_t mem_cache_size = shared->mem_cache_size;

	if (instance == 0)
		pr_dbg(stderr, "%s: using cache buffer size of %" PRIu64 "K\n",
			name, mem_cache_size / 1024);

#if defined(__linux__)
	if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
		pinned = true;
	else
		if (!CPU_COUNT(&proc_mask))
			pinned = true;

	if (pinned) {
		pr_inf(stderr, "%s: can't get sched affinity, pinning to CPU %d "
			"(instance %" PRIu32 ")\n",
			name, sched_getcpu(), pinned);
	}
#endif

	do {
		uint64_t i = mwc64() % mem_cache_size;
		uint64_t r = mwc64();
		register uint64_t j;

		if ((r >> 13) & 1) {
			switch (opt_flags & OPT_FLAGS_CACHE_MASK) {
			case OPT_FLAGS_CACHE_FLUSH:
				CACHE_WRITE(OPT_FLAGS_CACHE_FLUSH);
				break;
			case OPT_FLAGS_CACHE_FENCE:
				CACHE_WRITE(OPT_FLAGS_CACHE_FENCE);
				break;
			case OPT_FLAGS_CACHE_FENCE | OPT_FLAGS_CACHE_FLUSH:
				CACHE_WRITE(OPT_FLAGS_CACHE_FLUSH |
					    OPT_FLAGS_CACHE_FENCE);
				break;
			case OPT_FLAGS_CACHE_PREFETCH:
				CACHE_WRITE(OPT_FLAGS_CACHE_PREFETCH);
				break;
			case OPT_FLAGS_CACHE_PREFETCH | OPT_FLAGS_CACHE_FLUSH:
				CACHE_WRITE(OPT_FLAGS_CACHE_PREFETCH |
					    OPT_FLAGS_CACHE_FLUSH);
				break;
			case OPT_FLAGS_CACHE_PREFETCH | OPT_FLAGS_CACHE_FENCE:
				CACHE_WRITE(OPT_FLAGS_CACHE_PREFETCH |
					    OPT_FLAGS_CACHE_FENCE);
				break;
			case OPT_FLAGS_CACHE_PREFETCH | OPT_FLAGS_CACHE_FLUSH |
			     OPT_FLAGS_CACHE_FENCE:
				CACHE_WRITE(OPT_FLAGS_CACHE_PREFETCH |
					    OPT_FLAGS_CACHE_FLUSH |
					    OPT_FLAGS_CACHE_FENCE);
				break;
			default:
				CACHE_WRITE(0);
				break;
			}
		} else {
			for (j = 0; j < mem_cache_size; j++) {
				total += mem_cache[i] +
					mem_cache[(mem_cache_size - 1) - i];
				i = (i + 32769) % mem_cache_size;
				if (!opt_do_run)
					break;
			}
		}
#if defined(__linux__)
		if ((opt_flags & OPT_FLAGS_CACHE_NOAFF) && !pinned) {
			int current;

			/* Pin to the current CPU */
			current = sched_getcpu();
			if (current < 0)
				return EXIT_FAILURE;

			cpu = (int32_t)current;
		} else {
			do {
				cpu = (opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
					(mwc32() >> 4) : cpu + 1;
				cpu %= cpus;
			} while (!(CPU_ISSET(cpu, &proc_mask)));
		}

		if (!(opt_flags & OPT_FLAGS_CACHE_NOAFF) || !pinned) {
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);

			if ((opt_flags & OPT_FLAGS_CACHE_NOAFF)) {
				/* Don't continually set the affinity */
				pinned = true;
			}

		}
#endif
#if defined(__linux__) && defined(__NR_cacheflush)
		sys_cacheflush((char *)stress_cache, 8192, ICACHE);
		sys_cacheflush((char *)mem_cache, (int)mem_cache_size, DCACHE);
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_dbg(stderr, "%s: total [%" PRIu32 "]\n", name, total);
	return ret;
}
