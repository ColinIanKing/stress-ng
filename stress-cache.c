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
#if defined(_POSIX_PRIORITY_SCHEDULING) || defined(__linux__)
#include <sched.h>
#endif

#include "stress-ng.h"

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

static uint64_t mem_cache_size = 0;

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
	int ret = EXIT_SUCCESS;
#if defined(__linux__)
	unsigned long int cpu = 0;
	const unsigned long int cpus =
		stress_get_processors_configured();
	cpu_set_t mask;
	cpus_t *cpu_caches = NULL;
	cpu_cache_t *cache = NULL;
	int pinned = false;
	uint16_t max_cache_level = 0;

#endif
	(void)instance;

#if defined(__linux__)
	cpu_caches = get_all_cpu_cache_details ();
	if (!cpu_caches) {
		pr_inf(stderr, "%s: using built-in defaults as unable to "
			"determine cache details\n", name);
		shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	max_cache_level = get_max_cache_level(cpu_caches);

	if (shared->mem_cache_level > max_cache_level) {
		pr_inf(stderr, "%s: reducing cache level from %d (too high) "
			"to %d\n", name,
			shared->mem_cache_level, max_cache_level);
		shared->mem_cache_level = max_cache_level;
	}

	cache = get_cpu_cache(cpu_caches, shared->mem_cache_level);
	if (!cache) {
		pr_inf(stderr, "%s: using built-in defaults as no suitable "
			"cache found\n", name);
		free_cpu_caches(cpu_caches);
		shared->mem_cache_size = MEM_CACHE_SIZE;
		goto init_done;
	}

	if (shared->mem_cache_ways > 0) {
		uint64_t way_size;

		if (shared->mem_cache_ways > cache->ways) {
			pr_inf(stderr, "%s: cache way value too high - "
				"defaulting to %d (the maximum)\n",
				name, cache->ways);
			shared->mem_cache_ways = cache->ways;
		}

		way_size = cache->size / cache->ways;

		/* only fill the specified number of cache ways */
		shared->mem_cache_size = way_size * shared->mem_cache_ways;
	} else {
		/* fill the entire cache */
		shared->mem_cache_size = cache->size;
	}

	if (!shared->mem_cache_size) {
		pr_inf(stderr, "%s: using built-in defaults as unable to "
			"determine cache size\n", name);
		shared->mem_cache_size = MEM_CACHE_SIZE;
	}
#else
	shared->mem_cache_size = MEM_CACHE_SIZE;
#endif

#if defined(__linux__)
init_done:
#endif

	mem_cache_size = shared->mem_cache_size;

	shared->mem_cache = calloc(shared->mem_cache_size, 1);
	if (!shared->mem_cache) {
		pr_fail_err(name, "calloc");
		ret = EXIT_FAILURE;
		goto out;
	}

	uint8_t *mem_cache = shared->mem_cache;

	do {
		uint64_t i = mwc64() & (mem_cache_size - 1);
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
				i = (i + 32769) & (mem_cache_size - 1);
				if (!opt_do_run)
					break;
			}
		}
#if defined(__linux__)
		if ((opt_flags & OPT_FLAGS_CACHE_NOAFF) && !pinned) {
			int current;

			/* Pin to the current CPU */
			current = sched_getcpu();
			if (current < 0) {
				ret = EXIT_FAILURE;
				goto out;
			}

			cpu = current;
		} else {
			cpu = (opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
				(mwc32() >> 4) : cpu + 1;
			cpu %= cpus;
		}

		if (!(opt_flags & OPT_FLAGS_CACHE_NOAFF) || !pinned) {
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			sched_setaffinity(0, sizeof(mask), &mask);

			if ((opt_flags & OPT_FLAGS_CACHE_NOAFF)) {
				/* Don't continually set the affinity */
				pinned = true;
			}

		}
#endif
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	pr_dbg(stderr, "%s: total [%lu]\n", name, total);

out:
#if defined(__linux__)
	free_cpu_caches(cpu_caches);
#endif
	if (shared->mem_cache)
		free(shared->mem_cache);

	return ret;
}
