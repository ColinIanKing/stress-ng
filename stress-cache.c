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

#define FLAGS_CACHE_PREFETCH	(0x0001)
#define FLAGS_CACHE_CLFLUSH	(0x0002)
#define FLAGS_CACHE_FENCE	(0x0004)
#define FLAGS_CACHE_SFENCE	(0x0008)
#define FLAGS_CACHE_CLFLUSHOPT	(0x0010)
#define FLAGS_CACHE_CLDEMOTE	(0x0020)
#define FLAGS_CACHE_CLWB	(0x0040)
#define FLAGS_CACHE_NOAFF	(0x8000)

typedef void (*cache_write_func_t)(uint64_t inc, const uint64_t r, uint64_t *pi, uint64_t *pk);
typedef void (*cache_write_page_func_t)(uint8_t *const addr, const uint64_t size);

#define FLAGS_CACHE_MASK	(FLAGS_CACHE_PREFETCH |		\
				 FLAGS_CACHE_CLFLUSH |		\
				 FLAGS_CACHE_FENCE |		\
				 FLAGS_CACHE_SFENCE |		\
				 FLAGS_CACHE_CLFLUSHOPT |	\
				 FLAGS_CACHE_CLDEMOTE |		\
				 FLAGS_CACHE_CLWB)

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
	{ NULL, "cache-wb",		"cache line writeback (x86 only)" },
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

static int stress_cache_set_cldemote(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLDEMOTE);
}

static int stress_cache_set_clflushopt(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLFLUSHOPT);
}

static int stress_cache_set_clwb(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLWB);
}

static int stress_cache_set_fence(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_FENCE);
}

static int stress_cache_set_flush(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_CLFLUSH);
}

static int stress_cache_set_noaff(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_NOAFF);
}

static int stress_cache_set_prefetch(const char *opt)
{
	(void)opt;

	return stress_cache_set_flag(FLAGS_CACHE_PREFETCH);
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
	{ OPT_cache_cldemote,		stress_cache_set_cldemote },
	{ OPT_cache_clflushopt,		stress_cache_set_clflushopt },
	{ OPT_cache_enable_all,		stress_cache_set_enable_all },
	{ OPT_cache_fence,		stress_cache_set_fence },
	{ OPT_cache_flush,		stress_cache_set_flush },
	{ OPT_cache_no_affinity,	stress_cache_set_noaff },
	{ OPT_cache_prefetch,		stress_cache_set_prefetch },
	{ OPT_cache_sfence,		stress_cache_set_sfence },
	{ OPT_cache_clwb,		stress_cache_set_clwb },
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

#if defined(HAVE_ASM_X86_CLWB)
static inline void clwb(void *p)
{
        asm volatile("clwb (%0)\n" : : "r"(p) : "memory");
}
#define SHIM_CLWB(p)		clwb(p)
#else
#define SHIM_CLWB(p)
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
		if ((flags) & FLAGS_CACHE_CLWB) {			\
			SHIM_CLWB(&mem_cache[i]);			\
		}							\
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

#define CACHE_WRITE_USE_MOD(x)						\
static void OPTIMIZE3 stress_cache_write_mod_ ## x(			\
	const uint64_t inc, const uint64_t r, 				\
	uint64_t *pi, uint64_t *pk)					\
{									\
	register uint64_t i = *pi, j, k = *pk;				\
	uint8_t *const mem_cache = g_shared->mem_cache;			\
	const uint64_t mem_cache_size = g_shared->mem_cache_size;	\
									\
	CACHE_WRITE_MOD(x);						\
									\
	*pi = i;							\
	*pk = k;							\
}									\

CACHE_WRITE_USE_MOD(0x00)
CACHE_WRITE_USE_MOD(0x01)
CACHE_WRITE_USE_MOD(0x02)
CACHE_WRITE_USE_MOD(0x03)
CACHE_WRITE_USE_MOD(0x04)
CACHE_WRITE_USE_MOD(0x05)
CACHE_WRITE_USE_MOD(0x06)
CACHE_WRITE_USE_MOD(0x07)
CACHE_WRITE_USE_MOD(0x08)
CACHE_WRITE_USE_MOD(0x09)
CACHE_WRITE_USE_MOD(0x0a)
CACHE_WRITE_USE_MOD(0x0b)
CACHE_WRITE_USE_MOD(0x0c)
CACHE_WRITE_USE_MOD(0x0d)
CACHE_WRITE_USE_MOD(0x0e)
CACHE_WRITE_USE_MOD(0x0f)

CACHE_WRITE_USE_MOD(0x10)
CACHE_WRITE_USE_MOD(0x11)
CACHE_WRITE_USE_MOD(0x12)
CACHE_WRITE_USE_MOD(0x13)
CACHE_WRITE_USE_MOD(0x14)
CACHE_WRITE_USE_MOD(0x15)
CACHE_WRITE_USE_MOD(0x16)
CACHE_WRITE_USE_MOD(0x17)
CACHE_WRITE_USE_MOD(0x18)
CACHE_WRITE_USE_MOD(0x19)
CACHE_WRITE_USE_MOD(0x1a)
CACHE_WRITE_USE_MOD(0x1b)
CACHE_WRITE_USE_MOD(0x1c)
CACHE_WRITE_USE_MOD(0x1d)
CACHE_WRITE_USE_MOD(0x1e)
CACHE_WRITE_USE_MOD(0x1f)

CACHE_WRITE_USE_MOD(0x20)
CACHE_WRITE_USE_MOD(0x21)
CACHE_WRITE_USE_MOD(0x22)
CACHE_WRITE_USE_MOD(0x23)
CACHE_WRITE_USE_MOD(0x24)
CACHE_WRITE_USE_MOD(0x25)
CACHE_WRITE_USE_MOD(0x26)
CACHE_WRITE_USE_MOD(0x27)
CACHE_WRITE_USE_MOD(0x28)
CACHE_WRITE_USE_MOD(0x29)
CACHE_WRITE_USE_MOD(0x2a)
CACHE_WRITE_USE_MOD(0x2b)
CACHE_WRITE_USE_MOD(0x2c)
CACHE_WRITE_USE_MOD(0x2d)
CACHE_WRITE_USE_MOD(0x2e)
CACHE_WRITE_USE_MOD(0x2f)

CACHE_WRITE_USE_MOD(0x30)
CACHE_WRITE_USE_MOD(0x31)
CACHE_WRITE_USE_MOD(0x32)
CACHE_WRITE_USE_MOD(0x33)
CACHE_WRITE_USE_MOD(0x34)
CACHE_WRITE_USE_MOD(0x35)
CACHE_WRITE_USE_MOD(0x36)
CACHE_WRITE_USE_MOD(0x37)
CACHE_WRITE_USE_MOD(0x38)
CACHE_WRITE_USE_MOD(0x39)
CACHE_WRITE_USE_MOD(0x3a)
CACHE_WRITE_USE_MOD(0x3b)
CACHE_WRITE_USE_MOD(0x3c)
CACHE_WRITE_USE_MOD(0x3d)
CACHE_WRITE_USE_MOD(0x3e)
CACHE_WRITE_USE_MOD(0x3f)

CACHE_WRITE_USE_MOD(0x40)
CACHE_WRITE_USE_MOD(0x41)
CACHE_WRITE_USE_MOD(0x42)
CACHE_WRITE_USE_MOD(0x43)
CACHE_WRITE_USE_MOD(0x44)
CACHE_WRITE_USE_MOD(0x45)
CACHE_WRITE_USE_MOD(0x46)
CACHE_WRITE_USE_MOD(0x47)
CACHE_WRITE_USE_MOD(0x48)
CACHE_WRITE_USE_MOD(0x49)
CACHE_WRITE_USE_MOD(0x4a)
CACHE_WRITE_USE_MOD(0x4b)
CACHE_WRITE_USE_MOD(0x4c)
CACHE_WRITE_USE_MOD(0x4d)
CACHE_WRITE_USE_MOD(0x4e)
CACHE_WRITE_USE_MOD(0x4f)

CACHE_WRITE_USE_MOD(0x50)
CACHE_WRITE_USE_MOD(0x51)
CACHE_WRITE_USE_MOD(0x52)
CACHE_WRITE_USE_MOD(0x53)
CACHE_WRITE_USE_MOD(0x54)
CACHE_WRITE_USE_MOD(0x55)
CACHE_WRITE_USE_MOD(0x56)
CACHE_WRITE_USE_MOD(0x57)
CACHE_WRITE_USE_MOD(0x58)
CACHE_WRITE_USE_MOD(0x59)
CACHE_WRITE_USE_MOD(0x5a)
CACHE_WRITE_USE_MOD(0x5b)
CACHE_WRITE_USE_MOD(0x5c)
CACHE_WRITE_USE_MOD(0x5d)
CACHE_WRITE_USE_MOD(0x5e)
CACHE_WRITE_USE_MOD(0x5f)

CACHE_WRITE_USE_MOD(0x60)
CACHE_WRITE_USE_MOD(0x61)
CACHE_WRITE_USE_MOD(0x62)
CACHE_WRITE_USE_MOD(0x63)
CACHE_WRITE_USE_MOD(0x64)
CACHE_WRITE_USE_MOD(0x65)
CACHE_WRITE_USE_MOD(0x66)
CACHE_WRITE_USE_MOD(0x67)
CACHE_WRITE_USE_MOD(0x68)
CACHE_WRITE_USE_MOD(0x69)
CACHE_WRITE_USE_MOD(0x6a)
CACHE_WRITE_USE_MOD(0x6b)
CACHE_WRITE_USE_MOD(0x6c)
CACHE_WRITE_USE_MOD(0x6d)
CACHE_WRITE_USE_MOD(0x6e)
CACHE_WRITE_USE_MOD(0x6f)

CACHE_WRITE_USE_MOD(0x70)
CACHE_WRITE_USE_MOD(0x71)
CACHE_WRITE_USE_MOD(0x72)
CACHE_WRITE_USE_MOD(0x73)
CACHE_WRITE_USE_MOD(0x74)
CACHE_WRITE_USE_MOD(0x75)
CACHE_WRITE_USE_MOD(0x76)
CACHE_WRITE_USE_MOD(0x77)
CACHE_WRITE_USE_MOD(0x78)
CACHE_WRITE_USE_MOD(0x79)
CACHE_WRITE_USE_MOD(0x7a)
CACHE_WRITE_USE_MOD(0x7b)
CACHE_WRITE_USE_MOD(0x7c)
CACHE_WRITE_USE_MOD(0x7d)
CACHE_WRITE_USE_MOD(0x7e)
CACHE_WRITE_USE_MOD(0x7f)

static const cache_write_func_t cache_write_funcs[] = {
	stress_cache_write_mod_0x00,
	stress_cache_write_mod_0x01,
	stress_cache_write_mod_0x02,
	stress_cache_write_mod_0x03,
	stress_cache_write_mod_0x04,
	stress_cache_write_mod_0x05,
	stress_cache_write_mod_0x06,
	stress_cache_write_mod_0x07,
	stress_cache_write_mod_0x08,
	stress_cache_write_mod_0x09,
	stress_cache_write_mod_0x0a,
	stress_cache_write_mod_0x0b,
	stress_cache_write_mod_0x0c,
	stress_cache_write_mod_0x0d,
	stress_cache_write_mod_0x0e,
	stress_cache_write_mod_0x0f,

	stress_cache_write_mod_0x10,
	stress_cache_write_mod_0x11,
	stress_cache_write_mod_0x12,
	stress_cache_write_mod_0x13,
	stress_cache_write_mod_0x14,
	stress_cache_write_mod_0x15,
	stress_cache_write_mod_0x16,
	stress_cache_write_mod_0x17,
	stress_cache_write_mod_0x18,
	stress_cache_write_mod_0x19,
	stress_cache_write_mod_0x1a,
	stress_cache_write_mod_0x1b,
	stress_cache_write_mod_0x1c,
	stress_cache_write_mod_0x1d,
	stress_cache_write_mod_0x1e,
	stress_cache_write_mod_0x1f,

	stress_cache_write_mod_0x20,
	stress_cache_write_mod_0x21,
	stress_cache_write_mod_0x22,
	stress_cache_write_mod_0x23,
	stress_cache_write_mod_0x24,
	stress_cache_write_mod_0x25,
	stress_cache_write_mod_0x26,
	stress_cache_write_mod_0x27,
	stress_cache_write_mod_0x28,
	stress_cache_write_mod_0x29,
	stress_cache_write_mod_0x2a,
	stress_cache_write_mod_0x2b,
	stress_cache_write_mod_0x2c,
	stress_cache_write_mod_0x2d,
	stress_cache_write_mod_0x2e,
	stress_cache_write_mod_0x2f,

	stress_cache_write_mod_0x30,
	stress_cache_write_mod_0x31,
	stress_cache_write_mod_0x32,
	stress_cache_write_mod_0x33,
	stress_cache_write_mod_0x34,
	stress_cache_write_mod_0x35,
	stress_cache_write_mod_0x36,
	stress_cache_write_mod_0x37,
	stress_cache_write_mod_0x38,
	stress_cache_write_mod_0x39,
	stress_cache_write_mod_0x3a,
	stress_cache_write_mod_0x3b,
	stress_cache_write_mod_0x3c,
	stress_cache_write_mod_0x3d,
	stress_cache_write_mod_0x3e,
	stress_cache_write_mod_0x3f,

	stress_cache_write_mod_0x40,
	stress_cache_write_mod_0x41,
	stress_cache_write_mod_0x42,
	stress_cache_write_mod_0x43,
	stress_cache_write_mod_0x44,
	stress_cache_write_mod_0x45,
	stress_cache_write_mod_0x46,
	stress_cache_write_mod_0x47,
	stress_cache_write_mod_0x48,
	stress_cache_write_mod_0x49,
	stress_cache_write_mod_0x4a,
	stress_cache_write_mod_0x4b,
	stress_cache_write_mod_0x4c,
	stress_cache_write_mod_0x4d,
	stress_cache_write_mod_0x4e,
	stress_cache_write_mod_0x4f,

	stress_cache_write_mod_0x50,
	stress_cache_write_mod_0x51,
	stress_cache_write_mod_0x52,
	stress_cache_write_mod_0x53,
	stress_cache_write_mod_0x54,
	stress_cache_write_mod_0x55,
	stress_cache_write_mod_0x56,
	stress_cache_write_mod_0x57,
	stress_cache_write_mod_0x58,
	stress_cache_write_mod_0x59,
	stress_cache_write_mod_0x5a,
	stress_cache_write_mod_0x5b,
	stress_cache_write_mod_0x5c,
	stress_cache_write_mod_0x5d,
	stress_cache_write_mod_0x5e,
	stress_cache_write_mod_0x5f,

	stress_cache_write_mod_0x60,
	stress_cache_write_mod_0x61,
	stress_cache_write_mod_0x62,
	stress_cache_write_mod_0x63,
	stress_cache_write_mod_0x64,
	stress_cache_write_mod_0x65,
	stress_cache_write_mod_0x66,
	stress_cache_write_mod_0x67,
	stress_cache_write_mod_0x68,
	stress_cache_write_mod_0x69,
	stress_cache_write_mod_0x6a,
	stress_cache_write_mod_0x6b,
	stress_cache_write_mod_0x6c,
	stress_cache_write_mod_0x6d,
	stress_cache_write_mod_0x6e,
	stress_cache_write_mod_0x6f,

	stress_cache_write_mod_0x70,
	stress_cache_write_mod_0x71,
	stress_cache_write_mod_0x72,
	stress_cache_write_mod_0x73,
	stress_cache_write_mod_0x74,
	stress_cache_write_mod_0x75,
	stress_cache_write_mod_0x76,
	stress_cache_write_mod_0x77,
	stress_cache_write_mod_0x78,
	stress_cache_write_mod_0x79,
	stress_cache_write_mod_0x7a,
	stress_cache_write_mod_0x7b,
	stress_cache_write_mod_0x7c,
	stress_cache_write_mod_0x7d,
	stress_cache_write_mod_0x7e,
	stress_cache_write_mod_0x7f,
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

#if !defined(HAVE_ASM_X86_CLWB)
	if ((args->instance == 0) && (cache_flags & FLAGS_CACHE_CLWB)) {
		pr_inf("%s: clwb is not available, ignoring this option\n",
			args->name);
	}
#endif

#if defined(HAVE_ASM_X86_CLWB)
	if (!stress_cpu_x86_has_clwb() && (cache_flags & FLAGS_CACHE_CLWB)) {
		cache_flags &= ~FLAGS_CACHE_CLWB;
		if (args->instance == 0) {
			pr_inf("%s: clwb is not available, ignoring this option\n",
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
