/*
 * Copyright (C)      2022 Colin Ian King
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
#ifndef CORE_CACHE_H
#define CORE_CACHE_H

#include "core-arch.h"
#include "core-cpu.h"

/* Cache types */
typedef enum stress_cache_type {
	CACHE_TYPE_UNKNOWN = 0,		/* Unknown type */
	CACHE_TYPE_DATA,		/* D$ */
	CACHE_TYPE_INSTRUCTION,		/* I$ */
	CACHE_TYPE_UNIFIED,		/* D$ + I$ */
} stress_cache_type_t;

/* CPU cache information */
typedef struct stress_cpu_cache {
	uint64_t           size;      	/* cache size in bytes */
	uint32_t           line_size;	/* cache line size in bytes */
	uint32_t           ways;	/* cache ways */
	stress_cache_type_t type;	/* cache type */
	uint16_t           level;	/* cache level, L1, L2 etc */
	uint8_t		   padding[2];	/* padding */
} stress_cpu_cache_t;

typedef struct stress_cpu {
	stress_cpu_cache_t *caches;	/* CPU cache data */
	uint32_t	num;		/* CPU # number */
	uint32_t	cache_count;	/* CPU cache #  */
	bool		online;		/* CPU online when true */
	uint8_t		padding[7];	/* padding */
} stress_cpu_t;

typedef struct stress_cpus {
	stress_cpu_t *cpus;		/* CPU data */
	uint32_t	count;		/* CPU count */
	uint8_t		padding[4];	/* padding */
} stress_cpus_t;

/* CPU cache helpers */
extern stress_cpus_t *stress_get_all_cpu_cache_details(void);
extern uint16_t stress_get_max_cache_level(const stress_cpus_t *cpus);
extern stress_cpu_cache_t *stress_get_cpu_cache(const stress_cpus_t *cpus,
	const uint16_t cache_level);
extern void stress_free_cpu_caches(stress_cpus_t *cpus);

/*
 *  cacheflush(2) cache options
 */
#ifdef ICACHE
#define SHIM_ICACHE	(ICACHE)
#else
#define SHIM_ICACHE	(1 << 0)
#endif

#ifdef DCACHE
#define SHIM_DCACHE	(DCACHE)
#else
#define SHIM_DCACHE	(1 << 1)
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_CLFLUSH)

typedef void (*shim_clflush_func_t)(volatile void *ptr);

static inline void ALWAYS_INLINE shim_clflush_select(volatile void *ptr);
static shim_clflush_func_t shim_clflush_func =  shim_clflush_select;

static inline void ALWAYS_INLINE shim_clflush_op(volatile void *ptr)
{
	__asm__ __volatile__("clflush (%0)\n" : : "r"(ptr) : "memory");
}

static inline void ALWAYS_INLINE shim_clflush_nop(volatile void *ptr)
{
	(void)ptr;
}

static inline void ALWAYS_INLINE shim_clflush_select(volatile void *ptr)
{
	shim_clflush_func = stress_cpu_x86_has_clfsh() ? shim_clflush_op : shim_clflush_nop;

	shim_clflush_func(ptr);
}

/*
 *  shim_clflush()
 *	flush a cache line
 */
static inline void ALWAYS_INLINE shim_clflush(volatile void *ptr)
{
	shim_clflush_func(ptr);
}
#elif defined(DCACHE)
#define shim_clflush(ptr)	shim_cacheflush((char *)ptr, 64, DCACHE)
#else
#define shim_clflush(ptr)	do { } while (0) /* No-op */
#endif

#if !defined(HAVE_BUILTIN_PREFETCH) || defined(__PCC__)
/* a fake prefetch var-args no-op */
static inline void shim_builtin_prefetch(const void *addr, ...)
{
	va_list ap;

	va_start(ap, addr);
	va_end(ap);
}
#undef HAVE_BUILTIN_PREFETCH
#else
#define HAVE_BUILTIN_PREFETCH
#define shim_builtin_prefetch		__builtin_prefetch
#endif

/*
 *  shim_mfence()
 *	serializing memory fence
 */
static inline void ALWAYS_INLINE shim_mfence(void)
{
#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_FENCE) &&	\
    !defined(HAVE_SHIM_MFENCE)
	 __asm__ __volatile__("fence" ::: "memory");
#endif

#if defined(STRESS_ARCH_X86) &&		\
    !defined(HAVE_SHIM_MFENCE)
	__asm__ __volatile__("mfence" : : : "memory");
#define HAVE_SHIM_MFENCE
#endif

/* Disabled for now */
#if 0
#if defined(STRESS_ARCH_PPC64) &&	\
    !defined(HAVE_SHIM_MFENCE)
	__asm__ __volatile__ ("msync" : : : "memory");
#define HAVE_SHIM_MFENCE
#endif
#endif

#if defined(STRESS_ARCH_SPARC) &&	\
    !defined(HAVE_SHIM_MFENCE)
	 __asm__ __volatile__ ("membar #StoreLoad" : : : "memory");
#define HAVE_SHIM_MFENCE
#endif

#if defined(HAVE_SYNC_SYNCHRONIZE) &&	\
    !defined(HAVE_SHIM_MFENCE)
	__sync_synchronize();
#define HAVE_SHIM_MFENCE
#endif

	/* Other arches not yet implemented for older GCC flavours */
}

#endif
