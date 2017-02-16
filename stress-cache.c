/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include "stress-ng.h"

#ifndef ICACHE
#define	ICACHE	(1 << 0)
#endif
#ifndef DCACHE
#define DCACHE	(1 << 1)
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
		if (!g_keep_stressing_flag)				\
			break;						\
	}

/*
 *  stress_cache()
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
int stress_cache(const args_t *args)
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
	uint8_t *const mem_cache = g_shared->mem_cache;
	const uint64_t mem_cache_size = g_shared->mem_cache_size;

	if (args->instance == 0)
		pr_dbg("%s: using cache buffer size of %" PRIu64 "K\n",
			args->name, mem_cache_size / 1024);

#if defined(__linux__)
	if (sched_getaffinity(0, sizeof(proc_mask), &proc_mask) < 0)
		pinned = true;
	else
		if (!CPU_COUNT(&proc_mask))
			pinned = true;

	if (pinned) {
		pr_inf("%s: can't get sched affinity, pinning to "
			"CPU %d (instance %" PRIu32 ")\n",
			args->name, sched_getcpu(), pinned);
	}
#endif

	do {
		uint64_t i = mwc64() % mem_cache_size;
		uint64_t r = mwc64();
		register uint64_t j;

		if ((r >> 13) & 1) {
			switch (g_opt_flags & OPT_FLAGS_CACHE_MASK) {
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
				if (!g_keep_stressing_flag)
					break;
			}
		}
#if defined(__linux__)
		if ((g_opt_flags & OPT_FLAGS_CACHE_NOAFF) && !pinned) {
			int current;

			/* Pin to the current CPU */
			current = sched_getcpu();
			if (current < 0)
				return EXIT_FAILURE;

			cpu = (int32_t)current;
		} else {
			do {
				cpu = (g_opt_flags & OPT_FLAGS_AFFINITY_RAND) ?
					(mwc32() >> 4) : cpu + 1;
				cpu %= cpus;
			} while (!(CPU_ISSET(cpu, &proc_mask)));
		}

		if (!(g_opt_flags & OPT_FLAGS_CACHE_NOAFF) || !pinned) {
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);

			if ((g_opt_flags & OPT_FLAGS_CACHE_NOAFF)) {
				/* Don't continually set the affinity */
				pinned = true;
			}

		}
#endif
		shim_cacheflush((char *)stress_cache, 8192, ICACHE);
		shim_cacheflush((char *)mem_cache, (int)mem_cache_size, DCACHE);
		inc_counter(args);
	} while (keep_stressing());

	pr_dbg("%s: total [%" PRIu32 "]\n", args->name, total);
	return ret;
}
