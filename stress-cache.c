/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#define FLAGS_CACHE_SFENCE	(0x08)

typedef void (*cache_write_func_t)(uint64_t inc, const uint64_t r, uint64_t *pi, uint64_t *pk);
typedef void (*cache_write_page_func_t)(uint8_t *const addr, const uint64_t size);

#if defined(HAVE_BUILTIN_SFENCE)
#define FLAGS_CACHE_MASK	(FLAGS_CACHE_PREFETCH |	\
				 FLAGS_CACHE_FLUSH |	\
				 FLAGS_CACHE_FENCE |	\
				 FLAGS_CACHE_SFENCE)
#else
#define FLAGS_CACHE_MASK	(FLAGS_CACHE_PREFETCH |	\
				 FLAGS_CACHE_FLUSH |	\
				 FLAGS_CACHE_FENCE)
#endif

#define FLAGS_CACHE_NOAFF	(0x10)

static sigjmp_buf jmp_env;

static const stress_help_t help[] = {
	{ "C N","cache N",	 	"start N CPU cache thrashing workers" },
	{ NULL,	"cache-ops N",	 	"stop after N cache bogo operations" },
	{ NULL, "cache-no-affinity",	"do not change CPU affinity" },
	{ NULL,	"cache-fence",		"serialize stores" },
	{ NULL,	"cache-flush",		"flush cache after every memory write (x86 only)" },
	{ NULL,	"cache-level N",	"only exercise specified cache" },
	{ NULL,	"cache-prefetch",	"prefetch on memory reads/writes" },
#if defined(HAVE_BUILTIN_SFENCE)
	{ NULL,	"cache-sfence",		"serialize stores with sfence" },
#endif
	{ NULL,	"cache-ways N",		"only fill specified number of cache ways" },
	{ NULL,	NULL,		 NULL }
};

static int stress_cache_set_flag(const uint32_t flag)
{
	uint32_t cache_flags = 0;

	(void)stress_get_setting("cache-flags", &cache_flags);
	cache_flags |= flag;
	(void)stress_set_setting("cache-flags", TYPE_ID_UINT32, &cache_flags);

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

static int stress_cache_set_sfence(const char *opt)
{
	(void)opt;

#if defined(HAVE_BUILTIN_SFENCE)
	return stress_cache_set_flag(FLAGS_CACHE_SFENCE);
#else
	pr_inf("sfence not available, ignoring option --cache-sfence\n");
	return 0;
#endif
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_cache_prefetch,		stress_cache_set_prefetch },
	{ OPT_cache_flush,		stress_cache_set_flush },
	{ OPT_cache_fence,		stress_cache_set_fence },
	{ OPT_cache_sfence,		stress_cache_set_sfence },
	{ OPT_cache_no_affinity,	stress_cache_set_noaff },
	{ 0,				NULL }
};

#if defined(HAVE_BUILTIN_SFENCE)
#define SHIM_SFENCE()		__builtin_ia32_sfence()
#else
#define SHIM_SFENCE()
#endif

/*
 * The compiler optimises out the unused cache flush and mfence calls
 */
#define CACHE_WRITE_MOD(flags)						\
	for (j = 0; j < mem_cache_size; j++) {				\
		i += inc;						\
		i = (i >= mem_cache_size) ? i - mem_cache_size : i;	\
		k += 33;						\
		k = (k >= mem_cache_size) ? k - mem_cache_size : k;	\
									\
		if ((flags) & FLAGS_CACHE_PREFETCH) {			\
			shim_builtin_prefetch(&mem_cache[i + 1], 1, 1);	\
		}							\
		mem_cache[i] += mem_cache[k] + r;			\
		if ((flags) & FLAGS_CACHE_FLUSH) {			\
			shim_clflush(&mem_cache[i]);			\
		}							\
		if ((flags) & FLAGS_CACHE_FENCE) {			\
			shim_mfence();					\
		}							\
		if ((flags) & FLAGS_CACHE_SFENCE) {			\
			SHIM_SFENCE();					\
		}							\
		if (!keep_stressing_flag())				\
			break;						\
	}


#define CACHE_WRITE_USE_MOD(x, flags)					\
static void OPTIMIZE3 stress_cache_write_mod_ ## x(			\
	const uint64_t inc, const uint64_t r, 				\
	uint64_t *pi, uint64_t *pk)					\
{									\
	register uint64_t i = *pi, j, k = *pk;				\
	uint8_t *const mem_cache = g_shared->mem_cache;			\
	const uint64_t mem_cache_size = g_shared->mem_cache_size;	\
									\
	CACHE_WRITE_MOD(flags);						\
									\
	*pi = i;							\
	*pk = k;							\
}									\

CACHE_WRITE_USE_MOD(0,  0)
CACHE_WRITE_USE_MOD(1,  FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(2,  FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(3,  FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(4,  FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(5,  FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(6,  FLAGS_CACHE_FENCE | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(7,  FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(8,  FLAGS_CACHE_SFENCE)
CACHE_WRITE_USE_MOD(9,  FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(10, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(11, FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(12, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(13, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(14, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_FLUSH)
CACHE_WRITE_USE_MOD(15, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_FLUSH)

static const cache_write_func_t cache_write_funcs[] = {
	stress_cache_write_mod_0,
	stress_cache_write_mod_1,
	stress_cache_write_mod_2,
	stress_cache_write_mod_3,
	stress_cache_write_mod_4,
	stress_cache_write_mod_5,
	stress_cache_write_mod_6,
	stress_cache_write_mod_7,
	stress_cache_write_mod_8,
	stress_cache_write_mod_9,
	stress_cache_write_mod_10,
	stress_cache_write_mod_11,
	stress_cache_write_mod_12,
	stress_cache_write_mod_13,
	stress_cache_write_mod_14,
	stress_cache_write_mod_15,
};

typedef void (*cache_read_func_t)(uint64_t *pi, uint64_t *pk, uint32_t *ptotal);

static void NORETURN MLOCKED_TEXT stress_cache_sighandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
}

/*
 *  exercise invalid cache flush ops
 */
static void stress_cache_flush(void *addr, void *bad_addr, int size)
{
	(void)shim_cacheflush(addr, size, 0);
	(void)shim_cacheflush(addr, size, ~0);
	(void)shim_cacheflush(addr, 0, SHIM_DCACHE);
	(void)shim_cacheflush(addr, 1, SHIM_DCACHE);
	(void)shim_cacheflush(addr, -1, SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	__builtin___clear_cache(addr, addr);
#endif
	(void)shim_cacheflush(bad_addr, size, SHIM_ICACHE);
	(void)shim_cacheflush(bad_addr, size, SHIM_DCACHE);
	(void)shim_cacheflush(bad_addr, size, SHIM_ICACHE | SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	__builtin___clear_cache(addr, (void *)(addr - 1));
	__builtin___clear_cache(bad_addr, (void *)(bad_addr + size));
#endif
	shim_clflush(bad_addr);
}

/*
 *  stress_cache()
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
static int stress_cache(const stress_args_t *args)
{
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
	cpu_set_t mask;
	uint32_t cpu = 0;
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
	cpu_set_t proc_mask;
	bool pinned = false;
#endif
	uint32_t cache_flags = 0;
	uint32_t masked_flags;
	uint32_t total = 0;
	int ret = EXIT_SUCCESS;
	uint8_t *const mem_cache = g_shared->mem_cache;
	const uint64_t mem_cache_size = g_shared->mem_cache_size;
	uint64_t i = stress_mwc64() % mem_cache_size;
	uint64_t k = i + (mem_cache_size >> 1);
	uint64_t r = 0;
	uint64_t inc = (mem_cache_size >> 2) + 1;
	void *bad_addr;

	if (stress_sighandler(args->name, SIGSEGV, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGBUS , stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	(void)stress_get_setting("cache-flags", &cache_flags);
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

#if !defined(HAVE_BUILTIN_SFENCE)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_SFENCE)) {
		pr_inf("%s: sfence is not available, ignoring this option\n",
			args->name);
	}
#endif

	/*
	 *  map a page then unmap it, then we have an address
	 *  that is known to be not available. If the mapping
	 *  fails we have MAP_FAILED which too is an invalid
	 *  bad address.
	 */
	bad_addr = mmap(NULL, args->page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (bad_addr != MAP_FAILED)
		(void)munmap(bad_addr, args->page_size);

	masked_flags = cache_flags & FLAGS_CACHE_MASK;
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		r++;
		if (r & 1) {
			cache_write_funcs[masked_flags](inc, r, &i, &k);
		} else {
			register uint64_t j;

			for (j = 0; j < mem_cache_size; j++) {
				i += inc;
				i = (i >= mem_cache_size) ? i - mem_cache_size : i;
				k += 33;
				k = (k >= mem_cache_size) ? k - mem_cache_size : k;
				total += mem_cache[i] + mem_cache[k];
				if (!keep_stressing_flag())
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

			cpu = (uint32_t)current;
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
		(void)shim_cacheflush((char *)stress_cache, 8192, SHIM_ICACHE);
		(void)shim_cacheflush((char *)mem_cache, (int)mem_cache_size, SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
		__builtin___clear_cache((void *)stress_cache,
					(void *)((char *)stress_cache + 64));
#endif
		/*
		 * Periodically exercise invalid cache ops
		 */
		if ((r & 0x1f) == 0) {
			ret = sigsetjmp(jmp_env, 1);
			/*
			 *  We return here if we segfault, so
			 *  first check if we need to terminate
			 */
			if (!keep_stressing(args))
				break;

			if (!ret)
				stress_cache_flush(mem_cache, bad_addr, args->page_size);
		}
		inc_counter(args);

		/* Move forward a bit */
		i += inc;
		i = (i >= mem_cache_size) ? i - mem_cache_size : i;
	} while (keep_stressing(args));

	stress_uint32_put(total);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

stressor_info_t stress_cache_info = {
	.stressor = stress_cache,
	.class = CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
