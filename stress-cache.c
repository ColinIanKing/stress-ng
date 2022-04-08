/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2022 Colin Ian King
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
 */
#include "stress-ng.h"
#include "core-cache.h"
#include "core-put.h"

#define FLAGS_CACHE_PREFETCH	(0x01)
#define FLAGS_CACHE_CLFLUSH	(0x02)
#define FLAGS_CACHE_FENCE	(0x04)
#define FLAGS_CACHE_SFENCE	(0x08)
#define FLAGS_CACHE_CLFLUSHOPT	(0x10)
#define FLAGS_CACHE_CLDEMOTE	(0x20)
#define FLAGS_CACHE_NOAFF	(0x80)

typedef void (*cache_write_func_t)(uint64_t inc, const uint64_t r, uint64_t *pi, uint64_t *pk);
typedef void (*cache_write_page_func_t)(uint8_t *const addr, const uint64_t size);

#define FLAGS_CACHE_MASK	(FLAGS_CACHE_PREFETCH |		\
				 FLAGS_CACHE_CLFLUSH |		\
				 FLAGS_CACHE_FENCE |		\
				 FLAGS_CACHE_SFENCE |		\
				 FLAGS_CACHE_CLFLUSHOPT |	\
				 FLAGS_CACHE_CLDEMOTE)

typedef struct {
	uint32_t flag;		/* cache mask flag */
	const char *name;	/* human readable form */
} mask_flag_info_t;

static mask_flag_info_t mask_flag_info[] = {
	{ FLAGS_CACHE_PREFETCH,		"prefetch" },
	{ FLAGS_CACHE_CLFLUSH,		"clflush" },
	{ FLAGS_CACHE_FENCE,		"fence" },
	{ FLAGS_CACHE_SFENCE,		"sfence" },
	{ FLAGS_CACHE_CLFLUSHOPT,	"clflushopt" },
	{ FLAGS_CACHE_CLDEMOTE,		"cldemote" },
};

static sigjmp_buf jmp_env;
static volatile uint32_t masked_flags;
static uint64_t disabled_flags;

static const stress_help_t help[] = {
	{ "C N","cache N",	 	"start N CPU cache thrashing workers" },
	{ NULL,	"cache-ops N",	 	"stop after N cache bogo operations" },
#if defined(HAVE_ASM_X86_CLDEMOTE)
	{ NULL,	"cache-cldemote",	"cache line demote (x86 only)" },
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	{ NULL, "cache-clflushopt",	"optimized cache line flush (x86 only)" },
#endif
	{ NULL, "cache-enable-all",	"enable all cache options (fence,flush,sfence,etc..)" },
	{ NULL,	"cache-fence",		"serialize stores" },
#if defined(HAVE_ASM_X86_CLFLUSH)
	{ NULL,	"cache-flush",		"flush cache after every memory write (x86 only)" },
#endif
	{ NULL,	"cache-level N",	"only exercise specified cache" },
	{ NULL, "cache-no-affinity",	"do not change CPU affinity" },
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

static int stress_cache_set_enable_all(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_MASK);
}

static int stress_cache_set_prefetch(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_PREFETCH);
}

static int stress_cache_set_flush(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLFLUSH);
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

static int stress_cache_set_clflushopt(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLFLUSHOPT);
}

static int stress_cache_set_cldemote(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLDEMOTE);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_cache_cldemote,		stress_cache_set_cldemote },
	{ OPT_cache_clflushopt,		stress_cache_set_clflushopt },
	{ OPT_cache_enable_all,		stress_cache_set_enable_all },
	{ OPT_cache_fence,		stress_cache_set_fence },
	{ OPT_cache_flush,		stress_cache_set_flush },
	{ OPT_cache_no_affinity,	stress_cache_set_noaff },
	{ OPT_cache_prefetch,		stress_cache_set_prefetch },
	{ OPT_cache_sfence,		stress_cache_set_sfence },
	{ 0,				NULL }
};

#if defined(HAVE_BUILTIN_SFENCE)
#define SHIM_SFENCE()		__builtin_ia32_sfence()
#else
#define SHIM_SFENCE()
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static inline void clflush(void *p)
{
        asm volatile("clflush (%0)\n" : : "r"(p) : "memory");
}
#define SHIM_CLFLUSH(p)		clflush(p)
#else
#define SHIM_CLFLUSH(p)
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
static inline void clflushopt(void *p)
{
        asm volatile("clflushopt (%0)\n" : : "r"(p) : "memory");
}
#define SHIM_CLFLUSHOPT(p)	clflushopt(p)
#else
#define SHIM_CLFLUSHOPT(p)
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
static inline void cldemote(void *p)
{
        asm volatile("cldemote (%0)\n" : : "r"(p) : "memory");
}
#define SHIM_CLDEMOTE(p)	cldemote(p)
#else
#define SHIM_CLDEMOTE(p)
#endif

/*
 * The compiler optimises out the unused cache flush and mfence calls
 */
#define CACHE_WRITE_MOD(flags)						\
	for (j = 0; LIKELY(j < mem_cache_size); j++) {			\
		i += inc;						\
		i = (i >= mem_cache_size) ? i - mem_cache_size : i;	\
		k += 33;						\
		k = (k >= mem_cache_size) ? k - mem_cache_size : k;	\
									\
		if ((flags) & FLAGS_CACHE_PREFETCH) {			\
			shim_builtin_prefetch(&mem_cache[i + 1], 1, 1);	\
		}							\
		if ((flags) & FLAGS_CACHE_CLDEMOTE) {			\
			SHIM_CLDEMOTE(&mem_cache[i]);			\
		}							\
		if ((flags) & FLAGS_CACHE_CLFLUSHOPT) {			\
			SHIM_CLFLUSHOPT(&mem_cache[i]);			\
		}							\
		mem_cache[i] += mem_cache[k] + r;			\
		if ((flags) & FLAGS_CACHE_CLFLUSH) {			\
			SHIM_CLFLUSH(&mem_cache[i]);			\
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
CACHE_WRITE_USE_MOD(2,  FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(3,  FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(4,  FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(5,  FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(6,  FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(7,  FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(8,  FLAGS_CACHE_SFENCE)
CACHE_WRITE_USE_MOD(9,  FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(10, FLAGS_CACHE_SFENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(11, FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(12, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(13, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(14, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(15, FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(16, FLAGS_CACHE_CLFLUSHOPT)
CACHE_WRITE_USE_MOD(17, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(18, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(19, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(20, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(21, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(22, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(23, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(24, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE)
CACHE_WRITE_USE_MOD(25, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(26, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(27, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(28, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(29, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(30, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(31, FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(32, FLAGS_CACHE_CLDEMOTE | 0)
CACHE_WRITE_USE_MOD(33, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(34, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(35, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(36, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(37, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(38, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(39, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(40, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE)
CACHE_WRITE_USE_MOD(41, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(42, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(43, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(44, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(45, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(46, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(47, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(48, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT)
CACHE_WRITE_USE_MOD(49, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(50, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(51, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(52, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(53, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(54, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(55, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(56, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE)
CACHE_WRITE_USE_MOD(57, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(58, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(59, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(60, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE)
CACHE_WRITE_USE_MOD(61, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH)
CACHE_WRITE_USE_MOD(62, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_CLFLUSH)
CACHE_WRITE_USE_MOD(63, FLAGS_CACHE_CLDEMOTE | FLAGS_CACHE_CLFLUSHOPT | FLAGS_CACHE_SFENCE | FLAGS_CACHE_FENCE | FLAGS_CACHE_PREFETCH | FLAGS_CACHE_CLFLUSH)

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
	stress_cache_write_mod_16,
	stress_cache_write_mod_17,
	stress_cache_write_mod_18,
	stress_cache_write_mod_19,
	stress_cache_write_mod_20,
	stress_cache_write_mod_21,
	stress_cache_write_mod_22,
	stress_cache_write_mod_23,
	stress_cache_write_mod_24,
	stress_cache_write_mod_25,
	stress_cache_write_mod_26,
	stress_cache_write_mod_27,
	stress_cache_write_mod_28,
	stress_cache_write_mod_29,
	stress_cache_write_mod_30,
	stress_cache_write_mod_31,
	stress_cache_write_mod_32,
	stress_cache_write_mod_33,
	stress_cache_write_mod_34,
	stress_cache_write_mod_35,
	stress_cache_write_mod_36,
	stress_cache_write_mod_37,
	stress_cache_write_mod_38,
	stress_cache_write_mod_39,
	stress_cache_write_mod_40,
	stress_cache_write_mod_41,
	stress_cache_write_mod_42,
	stress_cache_write_mod_43,
	stress_cache_write_mod_44,
	stress_cache_write_mod_45,
	stress_cache_write_mod_46,
	stress_cache_write_mod_47,
	stress_cache_write_mod_48,
	stress_cache_write_mod_49,
	stress_cache_write_mod_50,
	stress_cache_write_mod_51,
	stress_cache_write_mod_52,
	stress_cache_write_mod_53,
	stress_cache_write_mod_54,
	stress_cache_write_mod_55,
	stress_cache_write_mod_56,
	stress_cache_write_mod_57,
	stress_cache_write_mod_58,
	stress_cache_write_mod_59,
	stress_cache_write_mod_60,
	stress_cache_write_mod_61,
	stress_cache_write_mod_62,
	stress_cache_write_mod_63,
};

typedef void (*cache_read_func_t)(uint64_t *pi, uint64_t *pk, uint32_t *ptotal);

static void NORETURN MLOCKED_TEXT stress_cache_sighandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
}

static void NORETURN MLOCKED_TEXT stress_cache_sigillhandler(int signum)
{
	(void)signum;
	size_t i = 0;
	uint32_t mask = masked_flags;

	/* Find top bit that is set, work from most modern flag to least */
	while (mask >>= 1)
		i++;
	mask = 1U << i;

	/* bit set? then disable it */
	if (mask) {
		for (i = 0; i < SIZEOF_ARRAY(mask_flag_info); i++) {
			if (mask_flag_info[i].flag & mask) {
				masked_flags &= ~mask;
				disabled_flags |= mask;
				break;
			}
		}
	}

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
#else
	UNEXPECTED
#endif
	(void)shim_cacheflush(bad_addr, size, SHIM_ICACHE);
	(void)shim_cacheflush(bad_addr, size, SHIM_DCACHE);
	(void)shim_cacheflush(bad_addr, size, SHIM_ICACHE | SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	__builtin___clear_cache(addr, (void *)((uint8_t *)addr - 1));
#else
	UNEXPECTED
#endif
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
	NOCLOBBER uint32_t cpu = 0;
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
	cpu_set_t proc_mask;
	NOCLOBBER bool pinned = false;
#endif
	uint32_t cache_flags = 0;
	NOCLOBBER uint32_t total = 0;
	int ret = EXIT_SUCCESS;
	uint8_t *const mem_cache = g_shared->mem_cache;
	const uint64_t mem_cache_size = g_shared->mem_cache_size;
	uint64_t i = stress_mwc64() % mem_cache_size;
	uint64_t k = i + (mem_cache_size >> 1);
	NOCLOBBER uint64_t r = 0;
	uint64_t inc = (mem_cache_size >> 2) + 1;
	void *bad_addr;

	disabled_flags = 0;

	if (sigsetjmp(jmp_env, 1)) {
		pr_inf("%s: premature SIGSEGV caught, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGBUS, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGILL, stress_cache_sigillhandler, NULL) < 0)
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
#else
	UNEXPECTED
#endif

#if !defined(HAVE_BUILTIN_SFENCE)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_SFENCE)) {
		pr_inf("%s: sfence is not available, ignoring this option\n",
			args->name);
	}
#endif

#if !defined(HAVE_ASM_X86_CLDEMOTE)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_CLDEMOTE)) {
		pr_inf("%s: cldemote is not available, ignoring this option\n",
			args->name);
	}
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
	if (!stress_cpu_x86_has_cldemote() && (cache_flags & FLAGS_CACHE_CLDEMOTE)) {
		cache_flags &= ~FLAGS_CACHE_CLDEMOTE;
		if (args->instance == 0) {
			pr_inf("%s: cldemote is not available, ignoring this option\n",
				args->name);
		}
	}
#endif

#if !defined(HAVE_ASM_X86_CLFLUSH)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_CLFLUSH)) {
		pr_inf("%s: clflush is not available, ignoring this option\n",
			args->name);
	}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
	if (!stress_cpu_x86_has_clfsh() && (cache_flags & FLAGS_CACHE_CLFLUSH)) {
		cache_flags &= ~FLAGS_CACHE_CLFLUSH;
		if (args->instance == 0) {
			pr_inf("%s: clflush is not available, ignoring this option\n",
				args->name);
		}
	}
#endif

#if !defined(HAVE_ASM_X86_CLFLUSHOPT)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_CLFLUSHOPT)) {
		pr_inf("%s: clflushopt is not available, ignoring this option\n",
			args->name);
	}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	if (!stress_cpu_x86_has_clflushopt() && (cache_flags & FLAGS_CACHE_CLFLUSHOPT)) {
		cache_flags &= ~FLAGS_CACHE_CLFLUSHOPT;
		if (args->instance == 0) {
			pr_inf("%s: clflushopt is not available, ignoring this option\n",
				args->name);
		}
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
		int jmpret;

		r++;

		jmpret = sigsetjmp(jmp_env, 1);
		/*
		 *  We return here if we segfault, so
		 *  check if we need to terminate
		 */
		if (jmpret) {
			if (keep_stressing(args))
				goto next;
			break;
		}
		if (r & 1) {
			uint32_t flags;

			flags = masked_flags ? masked_flags : ((stress_mwc32() & FLAGS_CACHE_MASK) & masked_flags);
			cache_write_funcs[flags](inc, r, &i, &k);
		} else {
			register volatile uint64_t j;

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
#else
		UNEXPECTED
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
			jmpret = sigsetjmp(jmp_env, 1);
			/*
			 *  We return here if we segfault, so
			 *  first check if we need to terminate
			 */
			if (!keep_stressing(args))
				break;

			if (!jmpret)
				stress_cache_flush(mem_cache, bad_addr, args->page_size);
		}
		inc_counter(args);

next:
		/* Move forward a bit */
		i += inc;
		i = (i >= mem_cache_size) ? i - mem_cache_size : i;

	} while (keep_stressing(args));

	/*
	 *  Hit an illegal instruction, report the disabled flags
	 */
	if ((args->instance == 0) && (disabled_flags)) {
		char buf[1024], *ptr = buf;
		size_t j, buf_len = sizeof(buf);

		(void)memset(buf, 0, sizeof(buf));
		for (j = 0; j < SIZEOF_ARRAY(mask_flag_info); j++) {
			if (mask_flag_info[j].flag & disabled_flags) {
				const size_t len = strlen(mask_flag_info[j].name);

				shim_strlcpy(ptr, " ", buf_len);
				buf_len--;
				ptr++;

				shim_strlcpy(ptr, mask_flag_info[j].name, buf_len);
				buf_len -= len;
				ptr+= len;
			}
		}
		*ptr = '\0';
		pr_inf("%s: disabled%s due to illegal instruction signal\n", args->name, buf);
	}

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
