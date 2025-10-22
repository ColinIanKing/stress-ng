/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2025 Colin Ian King
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
#include "core-asm-x86.h"
#include "core-asm-riscv.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-put.h"

#include <sched.h>

#define CACHE_FLAGS_PREFETCH	(0x0001U)
#define CACHE_FLAGS_CLFLUSH	(0x0002U)
#define CACHE_FLAGS_FENCE	(0x0004U)
#define CACHE_FLAGS_SFENCE	(0x0008U)
#define CACHE_FLAGS_CLFLUSHOPT	(0x0010U)
#define CACHE_FLAGS_CLDEMOTE	(0x0020U)
#define CACHE_FLAGS_CLWB	(0x0040U)
#define CACHE_FLAGS_PREFETCHW	(0x0080U)

#define CACHE_FLAGS_PERMUTE	(0x4000U)
#define CACHE_FLAGS_NOAFF	(0x8000U)

#define STRESS_CACHE_MIXED_OPS	(0)
#define STRESS_CACHE_READ	(1)
#define STRESS_CACHE_WRITE	(2)
#define STRESS_CACHE_MAX	(3)

typedef void (*cache_mixed_ops_func_t)(stress_args_t *args,
	uint64_t inc, const uint64_t r,
	uint64_t *pi, uint64_t *pk,
	stress_metrics_t *metrics);
typedef void (*cache_write_page_func_t)(uint8_t *const addr, const uint64_t size);

#define CACHE_FLAGS_MASK	(CACHE_FLAGS_PREFETCH |		\
				 CACHE_FLAGS_CLFLUSH |		\
				 CACHE_FLAGS_FENCE |		\
				 CACHE_FLAGS_SFENCE |		\
				 CACHE_FLAGS_CLFLUSHOPT |	\
				 CACHE_FLAGS_CLDEMOTE |		\
				 CACHE_FLAGS_CLWB | 		\
				 CACHE_FLAGS_PREFETCHW)

typedef struct {
	const uint32_t flag;	/* cache mask flag */
	const char *name;	/* human readable form */
} mask_flag_info_t;

static const stress_help_t help[] = {
	{ "C N","cache N",	 	"start N CPU cache thrashing workers" },
	{ NULL,	"cache-size N",		"override the default cache size setting to N bytes" },
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
	{ NULL,	"cache-ops N",	 	"stop after N cache bogo operations" },
	{ NULL,	"cache-permute",	"permute (mix) cache operations" },
	{ NULL,	"cache-prefetch",	"prefetch for memory reads/writes" },
#if defined(HAVE_ASM_X86_PREFETCHW)
	{ NULL,	"cache-prefetchw",	"prefetch for memory write" },
#endif
#if defined(HAVE_BUILTIN_SFENCE)
	{ NULL,	"cache-sfence",		"serialize stores with sfence" },
#endif
	{ NULL,	"cache-ways N",		"only fill specified number of cache ways" },
	{ NULL, "cache-clwb",		"cache line writeback (x86 only)" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_cache_cldemote,    "cache-cldemote",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_clflushopt,  "cache-cflushopt",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_enable_all,  "cache-enable-all",  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_fence,       "cache-fence",       TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_flush,	 "cache-flush",       TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_no_affinity, "cache-no-affinity", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_permute,	 "cache-permute",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_prefetch,    "cache-prefetch",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_prefetchw,   "cache-prefetchw",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_sfence,      "cache-sfence",      TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cache_clwb,        "cache-clb",         TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SIGLONGJMP)

static const mask_flag_info_t mask_flag_info[] = {
	{ CACHE_FLAGS_PREFETCH,		"prefetch" },
	{ CACHE_FLAGS_CLFLUSH,		"flush" },
	{ CACHE_FLAGS_FENCE,		"fence" },
	{ CACHE_FLAGS_SFENCE,		"sfence" },
	{ CACHE_FLAGS_CLFLUSHOPT,	"clflushopt" },
	{ CACHE_FLAGS_CLDEMOTE,		"cldemote" },
	{ CACHE_FLAGS_CLWB,		"clwb" },
	{ CACHE_FLAGS_PREFETCHW,	"prefetchw" },
};

static sigjmp_buf jmp_env;
static volatile int caught_signum;
static volatile uint32_t masked_flags;
static uint64_t disabled_flags;

#if defined(HAVE_BUILTIN_SFENCE)
#define SHIM_SFENCE()		__builtin_ia32_sfence()
#else
#define SHIM_SFENCE()
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
#define SHIM_CLFLUSH(p)		stress_asm_x86_clflush(p)
#else
#define SHIM_CLFLUSH(p)
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
#define SHIM_CLFLUSHOPT(p)	stress_asm_x86_clflushopt(p)
#else
#define SHIM_CLFLUSHOPT(p)
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
#define SHIM_CLDEMOTE(p)	stress_asm_x86_cldemote(p)
#else
#define SHIM_CLDEMOTE(p)
#endif

#if defined(HAVE_ASM_X86_CLWB)
#define SHIM_CLWB(p)		stress_asm_x86_clwb(p)
#else
#define SHIM_CLWB(p)
#endif

#if defined(HAVE_ASM_X86_PREFETCHW)
#define SHIM_PREFETCHW(p)	stress_asm_x86_prefetchw(p)
#else
#define SHIM_PREFETCHW(p)
#endif

/*
 * The compiler optimises out the unused cache flush and mfence calls
 */
#define CACHE_WRITE_MOD(flags)						\
	for (j = 0; LIKELY(j < buffer_size); j++) {			\
		i += inc;						\
		i = (i >= buffer_size) ? i - buffer_size : i;		\
		k += 33;						\
		k = (k >= buffer_size) ? k - buffer_size : k;		\
									\
		if ((flags) & CACHE_FLAGS_PREFETCH) {			\
			shim_builtin_prefetch(&buffer[i + 1], 1, 3);	\
		}							\
		if ((flags) & CACHE_FLAGS_CLDEMOTE) {			\
			SHIM_CLDEMOTE(&buffer[i]);			\
		}							\
		if ((flags) & CACHE_FLAGS_CLFLUSHOPT) {			\
			SHIM_CLFLUSHOPT(&buffer[i]);			\
		}							\
		buffer[i] += buffer[k] + r;				\
		if ((flags) & CACHE_FLAGS_CLWB) {			\
			SHIM_CLWB(&buffer[i]);				\
		}							\
		if ((flags) & CACHE_FLAGS_CLFLUSH) {			\
			SHIM_CLFLUSH(&buffer[i]);			\
		}							\
		if ((flags) & CACHE_FLAGS_FENCE) {			\
			shim_mfence();					\
		}							\
		if ((flags) & CACHE_FLAGS_SFENCE) {			\
			SHIM_SFENCE();					\
		}							\
		if ((flags) & CACHE_FLAGS_PREFETCHW) {			\
			SHIM_PREFETCHW(&buffer[i]);			\
		}							\
		if (UNLIKELY(!stress_continue_flag()))			\
			break;						\
	}

#define CACHE_WRITE_USE_MOD(x)						\
static void OPTIMIZE3 stress_cache_write_mod_ ## x(			\
	stress_args_t *args,						\
	const uint64_t inc,						\
	const uint64_t r, 						\
	uint64_t *pi,							\
	uint64_t *pk,							\
	stress_metrics_t *metrics)					\
{									\
	register uint64_t i = *pi, j, k = *pk;				\
	uint8_t *const buffer = g_shared->mem_cache.buffer;		\
	const uint64_t buffer_size = g_shared->mem_cache.size;		\
	double t;							\
									\
	t = stress_time_now();						\
	CACHE_WRITE_MOD(x);						\
	metrics->duration += stress_time_now() - t;			\
	metrics->count += (double)buffer_size;				\
	stress_bogo_add(args, j >> 10);					\
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

CACHE_WRITE_USE_MOD(0x80)
CACHE_WRITE_USE_MOD(0x81)
CACHE_WRITE_USE_MOD(0x82)
CACHE_WRITE_USE_MOD(0x83)
CACHE_WRITE_USE_MOD(0x84)
CACHE_WRITE_USE_MOD(0x85)
CACHE_WRITE_USE_MOD(0x86)
CACHE_WRITE_USE_MOD(0x87)
CACHE_WRITE_USE_MOD(0x88)
CACHE_WRITE_USE_MOD(0x89)
CACHE_WRITE_USE_MOD(0x8a)
CACHE_WRITE_USE_MOD(0x8b)
CACHE_WRITE_USE_MOD(0x8c)
CACHE_WRITE_USE_MOD(0x8d)
CACHE_WRITE_USE_MOD(0x8e)
CACHE_WRITE_USE_MOD(0x8f)

CACHE_WRITE_USE_MOD(0x90)
CACHE_WRITE_USE_MOD(0x91)
CACHE_WRITE_USE_MOD(0x92)
CACHE_WRITE_USE_MOD(0x93)
CACHE_WRITE_USE_MOD(0x94)
CACHE_WRITE_USE_MOD(0x95)
CACHE_WRITE_USE_MOD(0x96)
CACHE_WRITE_USE_MOD(0x97)
CACHE_WRITE_USE_MOD(0x98)
CACHE_WRITE_USE_MOD(0x99)
CACHE_WRITE_USE_MOD(0x9a)
CACHE_WRITE_USE_MOD(0x9b)
CACHE_WRITE_USE_MOD(0x9c)
CACHE_WRITE_USE_MOD(0x9d)
CACHE_WRITE_USE_MOD(0x9e)
CACHE_WRITE_USE_MOD(0x9f)

CACHE_WRITE_USE_MOD(0xa0)
CACHE_WRITE_USE_MOD(0xa1)
CACHE_WRITE_USE_MOD(0xa2)
CACHE_WRITE_USE_MOD(0xa3)
CACHE_WRITE_USE_MOD(0xa4)
CACHE_WRITE_USE_MOD(0xa5)
CACHE_WRITE_USE_MOD(0xa6)
CACHE_WRITE_USE_MOD(0xa7)
CACHE_WRITE_USE_MOD(0xa8)
CACHE_WRITE_USE_MOD(0xa9)
CACHE_WRITE_USE_MOD(0xaa)
CACHE_WRITE_USE_MOD(0xab)
CACHE_WRITE_USE_MOD(0xac)
CACHE_WRITE_USE_MOD(0xad)
CACHE_WRITE_USE_MOD(0xae)
CACHE_WRITE_USE_MOD(0xaf)

CACHE_WRITE_USE_MOD(0xb0)
CACHE_WRITE_USE_MOD(0xb1)
CACHE_WRITE_USE_MOD(0xb2)
CACHE_WRITE_USE_MOD(0xb3)
CACHE_WRITE_USE_MOD(0xb4)
CACHE_WRITE_USE_MOD(0xb5)
CACHE_WRITE_USE_MOD(0xb6)
CACHE_WRITE_USE_MOD(0xb7)
CACHE_WRITE_USE_MOD(0xb8)
CACHE_WRITE_USE_MOD(0xb9)
CACHE_WRITE_USE_MOD(0xba)
CACHE_WRITE_USE_MOD(0xbb)
CACHE_WRITE_USE_MOD(0xbc)
CACHE_WRITE_USE_MOD(0xbd)
CACHE_WRITE_USE_MOD(0xbe)
CACHE_WRITE_USE_MOD(0xbf)

CACHE_WRITE_USE_MOD(0xc0)
CACHE_WRITE_USE_MOD(0xc1)
CACHE_WRITE_USE_MOD(0xc2)
CACHE_WRITE_USE_MOD(0xc3)
CACHE_WRITE_USE_MOD(0xc4)
CACHE_WRITE_USE_MOD(0xc5)
CACHE_WRITE_USE_MOD(0xc6)
CACHE_WRITE_USE_MOD(0xc7)
CACHE_WRITE_USE_MOD(0xc8)
CACHE_WRITE_USE_MOD(0xc9)
CACHE_WRITE_USE_MOD(0xca)
CACHE_WRITE_USE_MOD(0xcb)
CACHE_WRITE_USE_MOD(0xcc)
CACHE_WRITE_USE_MOD(0xcd)
CACHE_WRITE_USE_MOD(0xce)
CACHE_WRITE_USE_MOD(0xcf)

CACHE_WRITE_USE_MOD(0xd0)
CACHE_WRITE_USE_MOD(0xd1)
CACHE_WRITE_USE_MOD(0xd2)
CACHE_WRITE_USE_MOD(0xd3)
CACHE_WRITE_USE_MOD(0xd4)
CACHE_WRITE_USE_MOD(0xd5)
CACHE_WRITE_USE_MOD(0xd6)
CACHE_WRITE_USE_MOD(0xd7)
CACHE_WRITE_USE_MOD(0xd8)
CACHE_WRITE_USE_MOD(0xd9)
CACHE_WRITE_USE_MOD(0xda)
CACHE_WRITE_USE_MOD(0xdb)
CACHE_WRITE_USE_MOD(0xdc)
CACHE_WRITE_USE_MOD(0xdd)
CACHE_WRITE_USE_MOD(0xde)
CACHE_WRITE_USE_MOD(0xdf)

CACHE_WRITE_USE_MOD(0xe0)
CACHE_WRITE_USE_MOD(0xe1)
CACHE_WRITE_USE_MOD(0xe2)
CACHE_WRITE_USE_MOD(0xe3)
CACHE_WRITE_USE_MOD(0xe4)
CACHE_WRITE_USE_MOD(0xe5)
CACHE_WRITE_USE_MOD(0xe6)
CACHE_WRITE_USE_MOD(0xe7)
CACHE_WRITE_USE_MOD(0xe8)
CACHE_WRITE_USE_MOD(0xe9)
CACHE_WRITE_USE_MOD(0xea)
CACHE_WRITE_USE_MOD(0xeb)
CACHE_WRITE_USE_MOD(0xec)
CACHE_WRITE_USE_MOD(0xed)
CACHE_WRITE_USE_MOD(0xee)
CACHE_WRITE_USE_MOD(0xef)

CACHE_WRITE_USE_MOD(0xf0)
CACHE_WRITE_USE_MOD(0xf1)
CACHE_WRITE_USE_MOD(0xf2)
CACHE_WRITE_USE_MOD(0xf3)
CACHE_WRITE_USE_MOD(0xf4)
CACHE_WRITE_USE_MOD(0xf5)
CACHE_WRITE_USE_MOD(0xf6)
CACHE_WRITE_USE_MOD(0xf7)
CACHE_WRITE_USE_MOD(0xf8)
CACHE_WRITE_USE_MOD(0xf9)
CACHE_WRITE_USE_MOD(0xfa)
CACHE_WRITE_USE_MOD(0xfb)
CACHE_WRITE_USE_MOD(0xfc)
CACHE_WRITE_USE_MOD(0xfd)
CACHE_WRITE_USE_MOD(0xfe)
CACHE_WRITE_USE_MOD(0xff)

static const cache_mixed_ops_func_t cache_mixed_ops_funcs[] = {
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

	stress_cache_write_mod_0x80,
	stress_cache_write_mod_0x81,
	stress_cache_write_mod_0x82,
	stress_cache_write_mod_0x83,
	stress_cache_write_mod_0x84,
	stress_cache_write_mod_0x85,
	stress_cache_write_mod_0x86,
	stress_cache_write_mod_0x87,
	stress_cache_write_mod_0x88,
	stress_cache_write_mod_0x89,
	stress_cache_write_mod_0x8a,
	stress_cache_write_mod_0x8b,
	stress_cache_write_mod_0x8c,
	stress_cache_write_mod_0x8d,
	stress_cache_write_mod_0x8e,
	stress_cache_write_mod_0x8f,

	stress_cache_write_mod_0x90,
	stress_cache_write_mod_0x91,
	stress_cache_write_mod_0x92,
	stress_cache_write_mod_0x93,
	stress_cache_write_mod_0x94,
	stress_cache_write_mod_0x95,
	stress_cache_write_mod_0x96,
	stress_cache_write_mod_0x97,
	stress_cache_write_mod_0x98,
	stress_cache_write_mod_0x99,
	stress_cache_write_mod_0x9a,
	stress_cache_write_mod_0x9b,
	stress_cache_write_mod_0x9c,
	stress_cache_write_mod_0x9d,
	stress_cache_write_mod_0x9e,
	stress_cache_write_mod_0x9f,

	stress_cache_write_mod_0xa0,
	stress_cache_write_mod_0xa1,
	stress_cache_write_mod_0xa2,
	stress_cache_write_mod_0xa3,
	stress_cache_write_mod_0xa4,
	stress_cache_write_mod_0xa5,
	stress_cache_write_mod_0xa6,
	stress_cache_write_mod_0xa7,
	stress_cache_write_mod_0xa8,
	stress_cache_write_mod_0xa9,
	stress_cache_write_mod_0xaa,
	stress_cache_write_mod_0xab,
	stress_cache_write_mod_0xac,
	stress_cache_write_mod_0xad,
	stress_cache_write_mod_0xae,
	stress_cache_write_mod_0xaf,

	stress_cache_write_mod_0xb0,
	stress_cache_write_mod_0xb1,
	stress_cache_write_mod_0xb2,
	stress_cache_write_mod_0xb3,
	stress_cache_write_mod_0xb4,
	stress_cache_write_mod_0xb5,
	stress_cache_write_mod_0xb6,
	stress_cache_write_mod_0xb7,
	stress_cache_write_mod_0xb8,
	stress_cache_write_mod_0xb9,
	stress_cache_write_mod_0xba,
	stress_cache_write_mod_0xbb,
	stress_cache_write_mod_0xbc,
	stress_cache_write_mod_0xbd,
	stress_cache_write_mod_0xbe,
	stress_cache_write_mod_0xbf,

	stress_cache_write_mod_0xc0,
	stress_cache_write_mod_0xc1,
	stress_cache_write_mod_0xc2,
	stress_cache_write_mod_0xc3,
	stress_cache_write_mod_0xc4,
	stress_cache_write_mod_0xc5,
	stress_cache_write_mod_0xc6,
	stress_cache_write_mod_0xc7,
	stress_cache_write_mod_0xc8,
	stress_cache_write_mod_0xc9,
	stress_cache_write_mod_0xca,
	stress_cache_write_mod_0xcb,
	stress_cache_write_mod_0xcc,
	stress_cache_write_mod_0xcd,
	stress_cache_write_mod_0xce,
	stress_cache_write_mod_0xcf,

	stress_cache_write_mod_0xd0,
	stress_cache_write_mod_0xd1,
	stress_cache_write_mod_0xd2,
	stress_cache_write_mod_0xd3,
	stress_cache_write_mod_0xd4,
	stress_cache_write_mod_0xd5,
	stress_cache_write_mod_0xd6,
	stress_cache_write_mod_0xd7,
	stress_cache_write_mod_0xd8,
	stress_cache_write_mod_0xd9,
	stress_cache_write_mod_0xda,
	stress_cache_write_mod_0xdb,
	stress_cache_write_mod_0xdc,
	stress_cache_write_mod_0xdd,
	stress_cache_write_mod_0xde,
	stress_cache_write_mod_0xdf,

	stress_cache_write_mod_0xe0,
	stress_cache_write_mod_0xe1,
	stress_cache_write_mod_0xe2,
	stress_cache_write_mod_0xe3,
	stress_cache_write_mod_0xe4,
	stress_cache_write_mod_0xe5,
	stress_cache_write_mod_0xe6,
	stress_cache_write_mod_0xe7,
	stress_cache_write_mod_0xe8,
	stress_cache_write_mod_0xe9,
	stress_cache_write_mod_0xea,
	stress_cache_write_mod_0xeb,
	stress_cache_write_mod_0xec,
	stress_cache_write_mod_0xed,
	stress_cache_write_mod_0xee,
	stress_cache_write_mod_0xef,

	stress_cache_write_mod_0xf0,
	stress_cache_write_mod_0xf1,
	stress_cache_write_mod_0xf2,
	stress_cache_write_mod_0xf3,
	stress_cache_write_mod_0xf4,
	stress_cache_write_mod_0xf5,
	stress_cache_write_mod_0xf6,
	stress_cache_write_mod_0xf7,
	stress_cache_write_mod_0xf8,
	stress_cache_write_mod_0xf9,
	stress_cache_write_mod_0xfa,
	stress_cache_write_mod_0xfb,
	stress_cache_write_mod_0xfc,
	stress_cache_write_mod_0xfd,
	stress_cache_write_mod_0xfe,
	stress_cache_write_mod_0xff,
};

typedef void (*cache_read_func_t)(uint64_t *pi, uint64_t *pk, uint32_t *ptotal);

static void NORETURN MLOCKED_TEXT stress_cache_sighandler(int signum)
{
	(void)signum;

	caught_signum = signum;

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
	stress_no_return();
}

static void NORETURN MLOCKED_TEXT stress_cache_sigillhandler(int signum)
{
	uint32_t mask = masked_flags;

	caught_signum = signum;

	/* bit set? then disable it */
	if (mask) {
		size_t i = 0;
		/* Find top bit that is set, work from most modern flag to least */
		while (mask >>= 1)
			i++;
		mask = 1U << i;

		for (i = 0; i < SIZEOF_ARRAY(mask_flag_info); i++) {
			if (mask_flag_info[i].flag & mask) {
				masked_flags &= ~mask;
				disabled_flags |= mask;
				break;
			}
		}
	}

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
	stress_no_return();
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

static void stress_cache_read(
	stress_args_t *args,
	const uint8_t *buffer,
	const uint64_t buffer_size,
	const uint64_t inc,
	uint64_t *i_ptr,
	uint64_t *k_ptr,
	stress_metrics_t *metrics_read)
{
	register uint64_t i = *i_ptr;
	register uint64_t j;
	register uint64_t k = *k_ptr;
	uint32_t total = 0;
	double t;

	t = stress_time_now();
	for (j = 0; j < buffer_size; j++) {
		i += inc;
		i = (i >= buffer_size) ? i - buffer_size : i;
		k += 33;
		k = (k >= buffer_size) ? k - buffer_size : k;
		total += buffer[i] + buffer[k];
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	metrics_read->duration += stress_time_now() - t;
	metrics_read->count += (double)(j + j); /* two reads per loop */
	stress_bogo_add(args, j >> 10);

	*i_ptr = i;
	*k_ptr = k;

	stress_uint32_put(total);
}

static void stress_cache_write(
	stress_args_t *args,
	uint8_t *buffer,
	const uint64_t buffer_size,
	const uint64_t inc,
	uint64_t *i_ptr,
	uint64_t *k_ptr,
	stress_metrics_t *metrics_write)
{
	register uint64_t i = *i_ptr;
	register uint64_t j;
	register uint64_t k = *k_ptr;
	uint32_t total = 0;
	double t;

	t = stress_time_now();
	for (j = 0; j < buffer_size; j++) {
		register const uint8_t v = j & 0xff;

		i += inc;
		i = (i >= buffer_size) ? i - buffer_size : i;
		k += 33;
		k = (k >= buffer_size) ? k - buffer_size : k;
		buffer[i] = v;
		buffer[k] = v;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	metrics_write->duration += stress_time_now() - t;
	metrics_write->count += (double)(j + j); /* 2 writes per loop */
	stress_bogo_add(args, j >> 10);

	*i_ptr = i;
	*k_ptr = k;

	stress_uint32_put(total);
}

static void stress_cached_str_flags(char *buf, size_t buflen, const uint32_t flags)
{
	size_t i;

	(void)shim_memset(buf, 0, buflen);
	for (i = 0; i < SIZEOF_ARRAY(mask_flag_info); i++) {
		if (flags & mask_flag_info[i].flag) {
			(void)shim_strlcat(buf, " ", buflen);
			(void)shim_strlcat(buf, mask_flag_info[i].name, buflen);
		}
	}
}

static void stress_cache_show_flags(
	stress_args_t *args,
	const uint32_t used_flags,
	const uint32_t ignored_flags)
{
	char buf[256];

	stress_cached_str_flags(buf, sizeof(buf), used_flags);
	if (!*buf)
		(void)shim_strscpy(buf, " none", sizeof(buf));
	pr_inf("%s: cache flags used:%s\n", args->name, buf);
	(void)shim_memset(buf, 0, sizeof(buf));

	stress_cached_str_flags(buf, sizeof(buf), ignored_flags);
	if (*buf)
		pr_inf("%s: unavailable unused cache flags:%s\n", args->name, buf);
}

static void stress_cache_bzero(uint8_t *buffer, const uint64_t buffer_size)
{
#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_CBO_ZERO) &&	\
    defined(__NR_riscv_hwprobe) && \
    defined(RISCV_HWPROBE_EXT_ZICBOZ)
	cpu_set_t cpus;
	struct riscv_hwprobe pair;

	(void)sched_getaffinity(0, sizeof(cpu_set_t), &cpus);

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;

	if (syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0) == 0) {
		if (pair.value & RISCV_HWPROBE_EXT_ZICBOZ) {
			pair.key = RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE;

			if (syscall(__NR_riscv_hwprobe, &pair, 1,
				    sizeof(cpu_set_t), &cpus, 0) == 0) {
				int block_size = (int)pair.value;
				register uint8_t *ptr;
				const uint8_t *buffer_end = buffer + buffer_size;

				for (ptr = buffer; ptr < buffer_end; ptr += block_size) {
					(void)stress_asm_riscv_cbo_zero((char *)ptr);
				}
			}
		}
	}
#else
	(void)buffer;
	(void)buffer_size;
#endif
}

static void stress_get_cache_flags(const char *opt, uint32_t *cache_flags, uint32_t bitmask)
{
	bool flag = 0;

	(void)stress_get_setting(opt, &flag);
	if (flag)
		*cache_flags |= bitmask;
}

/*
 *  stress_cache()
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
static int stress_cache(stress_args_t *args)
{
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
	cpu_set_t proc_mask;
	NOCLOBBER uint32_t cpu = 0;
	uint32_t *cpus;
	const uint32_t n_cpus = stress_get_usable_cpus(&cpus, true);
	NOCLOBBER bool pinned = false;
#endif
	NOCLOBBER uint32_t cache_flags = 0;
	NOCLOBBER uint32_t cache_flags_mask = CACHE_FLAGS_MASK;
	NOCLOBBER uint32_t ignored_flags = 0;
	NOCLOBBER uint32_t total = 0;
	NOCLOBBER size_t n_flags, idx_flags = 0;
	int ret = EXIT_SUCCESS;
	int *flag_permutations = NULL;
	uint8_t *const buffer = g_shared->mem_cache.buffer;
	const uint64_t buffer_size = g_shared->mem_cache.size;
	uint64_t i = stress_mwc64modn(buffer_size);
	uint64_t k = i + (buffer_size >> 1);
	NOCLOBBER uint64_t r = 0;
	uint64_t inc = (buffer_size >> 2) + 1;
	void *bad_addr;
	size_t j;
	stress_metrics_t metrics[STRESS_CACHE_MAX];

	static char *const metrics_description[] = {
		"cache ops per second",
		"shared cache reads per second",
		"shared cache writes per second",
	};

	stress_zero_metrics(metrics, SIZEOF_ARRAY(metrics));

	caught_signum = -1;
	disabled_flags = 0;

	(void)cache_flags;
	(void)cache_flags_mask;

	if (sigsetjmp(jmp_env, 1)) {
		const char *signame = stress_get_signal_name(caught_signum);

		pr_inf_skip("%s: signal %s (#%d) caught, skipping stressor\n",
			args->name, signame ? signame : "unknown", caught_signum);
		ret = EXIT_NO_RESOURCE;
		goto tidy_cpus;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_cache_sighandler, NULL) < 0) {
		ret = EXIT_NO_RESOURCE;
		goto tidy_cpus;
	}
#if !defined(STRESS_ARCH_X86)
	if (stress_sighandler(args->name, SIGBUS, stress_cache_sighandler, NULL) < 0) {
		ret = EXIT_NO_RESOURCE;
		goto tidy_cpus;
	}
#endif
	if (stress_sighandler(args->name, SIGILL, stress_cache_sigillhandler, NULL) < 0) {
		ret = EXIT_NO_RESOURCE;
		goto tidy_cpus;
	}

	(void)stress_get_cache_flags("cache-cldemote", &cache_flags, CACHE_FLAGS_CLDEMOTE);
	(void)stress_get_cache_flags("cache-cflushopt", &cache_flags, CACHE_FLAGS_CLFLUSHOPT);
	(void)stress_get_cache_flags("cache-enable-all", &cache_flags, CACHE_FLAGS_MASK);
	(void)stress_get_cache_flags("cache-fence", &cache_flags, CACHE_FLAGS_FENCE);
	(void)stress_get_cache_flags("cache-flush", &cache_flags, CACHE_FLAGS_CLFLUSH);
	(void)stress_get_cache_flags("cache-no-affinity", &cache_flags, CACHE_FLAGS_NOAFF);
	(void)stress_get_cache_flags("cache-prefetch", &cache_flags, CACHE_FLAGS_PREFETCH);
	(void)stress_get_cache_flags("cache-sfence", &cache_flags, CACHE_FLAGS_SFENCE);
	(void)stress_get_cache_flags("cache-clb", &cache_flags, CACHE_FLAGS_CLWB);
	(void)stress_get_cache_flags("cache-prefetchw", &cache_flags, CACHE_FLAGS_PREFETCHW);
	(void)stress_get_cache_flags("cache-permute", &cache_flags, CACHE_FLAGS_PERMUTE);

	if (stress_instance_zero(args))
		pr_dbg("%s: using cache buffer size of %" PRIu64 "K\n",
			args->name, buffer_size / 1024);

#if defined(HAVE_SCHED_GETAFFINITY) && 	\
    defined(HAVE_SCHED_SETAFFINITY) &&	\
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

#if !defined(HAVE_SHIM_MFENCE)
	if (cache_flags & CACHE_FLAGS_FENCE)
		ignored_flags |= CACHE_FLAGS_FENCE;
	cache_flags &= ~CACHE_FLAGS_FENCE;
	cache_flags_mask &= ~CACHE_FLAGS_FENCE;
#endif

#if !defined(HAVE_BUILTIN_SFENCE)
	if (cache_flags & CACHE_FLAGS_SFENCE)
		ignored_flags |= CACHE_FLAGS_SFENCE;
	cache_flags &= ~CACHE_FLAGS_SFENCE;
	cache_flags_mask &= ~CACHE_FLAGS_SFENCE;
#endif

#if !defined(HAVE_ASM_X86_CLDEMOTE)
	if (cache_flags & CACHE_FLAGS_CLDEMOTE)
		ignored_flags |= CACHE_FLAGS_CLDEMOTE;
	cache_flags &= ~CACHE_FLAGS_CLDEMOTE;
	cache_flags_mask &= ~CACHE_FLAGS_CLDEMOTE;
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
	if (!stress_cpu_x86_has_cldemote() && (cache_flags & CACHE_FLAGS_CLDEMOTE)) {
		cache_flags &= ~CACHE_FLAGS_CLDEMOTE;
		cache_flags_mask &= ~CACHE_FLAGS_CLDEMOTE;
		ignored_flags |= CACHE_FLAGS_CLDEMOTE;
	}
#endif

#if !defined(HAVE_ASM_X86_CLFLUSH)
	if (cache_flags & CACHE_FLAGS_CLFLUSH)
		ignored_flags |= CACHE_FLAGS_CLFLUSH;
	cache_flags &= ~CACHE_FLAGS_CLFLUSH;
	cache_flags_mask &= ~CACHE_FLAGS_CLFLUSH;
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
	if (!stress_cpu_x86_has_clfsh() && (cache_flags & CACHE_FLAGS_CLFLUSH)) {
		cache_flags &= ~CACHE_FLAGS_CLFLUSH;
		cache_flags_mask &= ~CACHE_FLAGS_CLFLUSH;
		ignored_flags |= CACHE_FLAGS_CLFLUSH;
	}
#endif

#if !defined(HAVE_ASM_X86_CLFLUSHOPT)
	if (cache_flags & CACHE_FLAGS_CLFLUSHOPT)
		ignored_flags |= CACHE_FLAGS_CLFLUSHOPT;
	cache_flags &= ~CACHE_FLAGS_CLFLUSHOPT;
	cache_flags_mask &= ~CACHE_FLAGS_CLFLUSHOPT;
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	if (!stress_cpu_x86_has_clflushopt() && (cache_flags & CACHE_FLAGS_CLFLUSHOPT)) {
		cache_flags &= ~CACHE_FLAGS_CLFLUSHOPT;
		cache_flags_mask &= ~CACHE_FLAGS_CLFLUSHOPT;
		ignored_flags |= CACHE_FLAGS_CLFLUSHOPT;
	}
#endif

#if !defined(HAVE_ASM_X86_CLWB)
	if (cache_flags & CACHE_FLAGS_CLWB)
		ignored_flags |= CACHE_FLAGS_CLWB;
	cache_flags &= ~CACHE_FLAGS_CLWB;
	cache_flags_mask &= ~CACHE_FLAGS_CLWB;
#endif

#if defined(HAVE_ASM_X86_CLWB)
	if (!stress_cpu_x86_has_clwb() && (cache_flags & CACHE_FLAGS_CLWB)) {
		cache_flags &= ~CACHE_FLAGS_CLWB;
		cache_flags_mask &= ~CACHE_FLAGS_CLWB;
		ignored_flags |= CACHE_FLAGS_CLWB;
	}
#endif

/* Note that x86 without prefetchw implemented is a no-op */
#if !defined(HAVE_ASM_X86_PREFETCHW)
	if (cache_flags & CACHE_FLAGS_PREFETCHW)
		ignored_flags |= CACHE_FLAGS_PREFETCHW;
	cache_flags &= ~CACHE_FLAGS_PREFETCHW;
	cache_flags_mask &= ~CACHE_FLAGS_PREFETCHW;
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

	masked_flags = cache_flags & CACHE_FLAGS_MASK;
	if (stress_instance_zero(args)) {
		stress_cache_show_flags(args, masked_flags, ignored_flags);
		if (masked_flags == 0)
			pr_inf("%s: use --cache-enable-all to enable all cache flags for heavier cache stressing\n", args->name);
	}
	(void)shim_memset(buffer, 0, buffer_size);
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (masked_flags) {
		n_flags = stress_flag_permutation(masked_flags, &flag_permutations);
		if ((n_flags > 0) && (stress_instance_zero(args)))
			pr_inf("%s: %zu permutations of cache flags being exercised\n",
				args->name, n_flags);
	}

	do {
		int jmpret;
		uint32_t flags;

		jmpret = sigsetjmp(jmp_env, 1);
		/*
		 *  We return here if we segfault, so
		 *  check if we need to terminate
		 */
		if (jmpret) {
			if (LIKELY(stress_continue(args)))
				goto next;
			break;
		}
		switch (r) {
		case STRESS_CACHE_MIXED_OPS:
			if ((masked_flags) && (n_flags > 0)) {
				cache_mixed_ops_funcs[flag_permutations[idx_flags]](args, inc, r, &i, &k, &metrics[STRESS_CACHE_MIXED_OPS]);
				idx_flags++;
				if (idx_flags >= n_flags)
					idx_flags = 0;

			} else {
				flags = masked_flags ? masked_flags : ((stress_mwc32() & CACHE_FLAGS_MASK) & masked_flags);
				cache_mixed_ops_funcs[flags](args, inc, r, &i, &k, &metrics[STRESS_CACHE_MIXED_OPS]);
			}
			break;
		case STRESS_CACHE_READ:
			stress_cache_read(args, buffer, buffer_size, inc, &i, &k, &metrics[STRESS_CACHE_READ]);
			break;
		case STRESS_CACHE_WRITE:
			stress_cache_write(args, buffer, buffer_size, inc, &i, &k, &metrics[STRESS_CACHE_WRITE]);
			break;
		}
		r++;
		if (r >= STRESS_CACHE_MAX)
			r = 0;
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
		if ((cache_flags & CACHE_FLAGS_NOAFF) && !pinned) {
			const int current = sched_getcpu();

			if (current < 0) {
				pr_fail("%s: getcpu failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				ret = EXIT_FAILURE;
				goto tidy_cpus;
			}
			cpu = (uint32_t)current;
		} else {
			static uint32_t cpu_idx = 0;

			if (cpus) {
				cpu = cpus[cpu_idx];
				cpu_idx++;
				cpu_idx = (cpu_idx >= n_cpus) ? 0 : cpu_idx;
			} else {
				const int current = sched_getcpu();

				if (current < 0) {
					pr_fail("%s: getcpu failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					ret = EXIT_FAILURE;
					goto tidy_cpus;
				}
				cpu = (uint32_t)current;
			}
		}

		if (!(cache_flags & CACHE_FLAGS_NOAFF) || !pinned) {
			cpu_set_t mask;

			CPU_ZERO(&mask);
			CPU_SET(cpu, &mask);
			(void)sched_setaffinity(0, sizeof(mask), &mask);

			if ((cache_flags & CACHE_FLAGS_NOAFF)) {
				/* Don't continually set the affinity */
				pinned = true;
			}

		}
#else
		UNEXPECTED
#endif
		(void)shim_cacheflush((char *)stress_cache, 8192, SHIM_ICACHE);
		(void)shim_cacheflush((char *)buffer, (int)buffer_size, SHIM_DCACHE);
		stress_cache_bzero(buffer, buffer_size);
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
			if (UNLIKELY(!stress_continue(args)))
				break;

			if (!jmpret)
				stress_cache_flush(buffer, bad_addr, (int)args->page_size);
		}
next:
		/* Move forward a bit */
		i += inc;
		i = (i >= buffer_size) ? i - buffer_size : i;

	} while (stress_continue(args));

	/*
	 *  Hit an illegal instruction, report the disabled flags
	 */
	if (stress_instance_zero(args) && (disabled_flags)) {
		char buf[1024], *ptr = buf;
		size_t buf_len = sizeof(buf);

		(void)shim_memset(buf, 0, sizeof(buf));
		for (j = 0; j < SIZEOF_ARRAY(mask_flag_info); j++) {
			if (mask_flag_info[j].flag & disabled_flags) {
				const size_t len = strlen(mask_flag_info[j].name);

				(void)shim_strscpy(ptr, " ", buf_len);
				buf_len--;
				ptr++;

				(void)shim_strscpy(ptr, mask_flag_info[j].name, buf_len);
				buf_len -= len;
				ptr += len;
			}
		}
		*ptr = '\0';
		pr_inf("%s: disabled%s due to illegal instruction signal\n", args->name, buf);
	}

	stress_uint32_put(total);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (j = 0; j < SIZEOF_ARRAY(metrics); j++) {
		const double rate = metrics[j].duration > 0.0 ?
			metrics[j].count / metrics[j].duration : 0.0;

		stress_metrics_set(args, j, metrics_description[j],
			rate, STRESS_METRIC_HARMONIC_MEAN);

		if (j == 0)
			pr_dbg("%s: %.2f %s\n", args->name, rate, metrics_description[j]);
	}
tidy_cpus:
	if (flag_permutations)
		free(flag_permutations);
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY) &&	\
    defined(HAVE_SCHED_GETCPU)
	stress_free_usable_cpus(&cpus);
#endif

	return ret;
}

const stressor_info_t stress_cache_info = {
	.stressor = stress_cache,
	.classifier = CLASS_CPU_CACHE,
	.opts = opts,
	.help = help
};

#else

const stressor_info_t stress_cache_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
