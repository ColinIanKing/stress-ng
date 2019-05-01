/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

#define FLAGS_CACHE_PREFETCH	(0x01)
#define FLAGS_CACHE_FLUSH	(0x02)
#define FLAGS_CACHE_FENCE	(0x04)
#define FLAGS_CACHE_NOAFF	(0x08)

static const help_t help[] = {
	{ "C N","cache N",	 "start N CPU cache thrashing workers" },
	{ NULL,	"cache-ops N",	 "stop after N cache bogo operations" },
	{ NULL,	"cache-prefetch","prefetch on memory reads/writes" },
	{ NULL,	"cache-flush",	 "flush cache after every memory write (x86 only)" },
	{ NULL,	"cache-fence",	 "serialize stores" },
	{ NULL,	"cache-level N", "only exercise specified cache" },
	{ NULL,	"cache-ways N",	 "only fill specified number of cache ways" },
	{ NULL,	NULL,		 NULL }
};

static int stress_cache_set_flag(const uint32_t flag)
{
	uint32_t cache_flags = 0;

	(void)get_setting("cache-flags", &cache_flags);
	cache_flags |= flag;
	(void)set_setting("cache-flags", TYPE_ID_UINT32, &cache_flags);

	return 0;
}

static int stress_cache_set_prefetch(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_PREFETCH);
}

static int stress_cache_set_flush(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_FLUSH);
}

static int stress_cache_set_fence(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_FENCE);
}

static int stress_cache_set_noaff(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_NOAFF);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_cache_prefetch,		stress_cache_set_prefetch },
	{ OPT_cache_flush,		stress_cache_set_flush },
	{ OPT_cache_fence,		stress_cache_set_fence },
	{ OPT_cache_no_affinity,	stress_cache_set_noaff },
	{ 0,				NULL }
};


/* The compiler optimises out the unused cache flush and mfence calls */
#define CACHE_WRITE(flag)						\
	for (j = 0; j < mem_cache_size; j++) {				\
		if ((flag) & FLAGS_CACHE_PREFETCH) {		\
			__builtin_prefetch(&mem_cache[i + 1], 1, 1);	\
		}							\
		mem_cache[i] += mem_cache[(mem_cache_size - 1) - i] + r;\
		if ((flag) & FLAGS_CACHE_FLUSH) {			\
			clflush(&mem_cache[i]);				\
		}							\
		if ((flag) & FLAGS_CACHE_FENCE) {			\
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
static int stress_cache(const args_t *args)
{
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
	cpu_set_t mask;
	uint32_t cpu = 0;
	const uint32_t cpus = stress_get_processors_configured();
	cpu_set_t proc_mask;
	bool pinned = false;
#endif
	uint32_t cache_flags = 0;
	uint32_t total = 0;
	int ret = EXIT_SUCCESS;
	uint8_t *const mem_cache = g_shared->mem_cache;
	const uint64_t mem_cache_size = g_shared->mem_cache_size;

	(void)get_setting("cache-flags", &cache_flags);
	if (args->instance == 0)
		pr_dbg("%s: using cache buffer size of %" PRIu64 "K\n",
			args->name, mem_cache_size / 1024);

#if defined(HAVE_SCHED_GETAFFINITY) && 	\
    defined(HAVE_SCHED_GETCPU)
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
			switch (cache_flags) {
			case FLAGS_CACHE_FLUSH:
				CACHE_WRITE(FLAGS_CACHE_FLUSH);
				break;
			case FLAGS_CACHE_FENCE:
				CACHE_WRITE(FLAGS_CACHE_FENCE);
				break;
			case FLAGS_CACHE_FENCE | FLAGS_CACHE_FLUSH:
				CACHE_WRITE(FLAGS_CACHE_FLUSH | FLAGS_CACHE_FENCE);
				break;
			case FLAGS_CACHE_PREFETCH:
				CACHE_WRITE(FLAGS_CACHE_PREFETCH);
				break;
			case FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH:
				CACHE_WRITE(FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH);
				break;
			case FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FENCE:
				CACHE_WRITE(FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FENCE);
				break;
			case FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH | FLAGS_CACHE_FENCE:
				CACHE_WRITE(FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH | FLAGS_CACHE_FENCE);
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
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
		if ((cache_flags & FLAGS_CACHE_NOAFF) && !pinned) {
			int current;

			/* Pin to the current CPU */
			current = sched_getcpu();
			if (current < 0)
				return EXIT_FAILURE;

			cpu = (int32_t)current;
		} else {
			do {
				cpu++;
				cpu %= cpus;
			} while (!(CPU_ISSET(cpu, &proc_mask)));
		}

		if (!(cache_flags & FLAGS_CACHE_NOAFF) || !pinned) {
			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);

			if ((cache_flags & FLAGS_CACHE_NOAFF)) {
				/* Don't continually set the affinity */
				pinned = true;
			}

		}
#endif
		(void)shim_cacheflush((char *)stress_cache, 8192, ICACHE);
		(void)shim_cacheflush((char *)mem_cache, (int)mem_cache_size, DCACHE);
		inc_counter(args);
	} while (keep_stressing());

	pr_dbg("%s: total [%" PRIu32 "]\n", args->name, total);
	return ret;
}

stressor_info_t stress_cache_info = {
	.stressor = stress_cache,
	.class = CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
