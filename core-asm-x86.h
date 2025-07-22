/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_ASM_X86_H
#define CORE_ASM_X86_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(__LP64) || defined(__LP64__)
#define STRESS_ARCH_X86_LP64
#endif

#if defined(__LP32) || defined(__LP32__)
#define STRESS_ARCH_X86_LP32
#endif

#if defined(STRESS_ARCH_X86)
#define stress_asm_x86_lock_add(ptr, inc) 		\
	do {						\
		__asm__ __volatile__(			\
			"lock addl %1,%0"		\
			: "+m" (*ptr)			\
			: "ir" (inc));			\
	} while (0)

/*
 *  Position independent code uses %ebx, so we need
 *  to swap it with a temp register so as not to
 *  clobber it with cpuid.
 */
#if (defined(__pie__) || defined(__PIE__)) &&		\
    defined(STRESS_ARCH_X86_32) && 			\
    !NEED_GNUC(5, 0, 0)
#define stress_asm_x86_cpuid(a, b, c, d)		\
	do {						\
		__asm__ __volatile__ (			\
			"xchg{l} {%%}ebx, %k1\n"	\
			"cpuid\n"			\
			"xchg{l} {%%}ebx, %k1\n"	\
			: "=a"(a),			\
			  "=&r"(b),			\
			  "=c"(c),			\
			  "=d"(d)			\
			: "0"(a),"2"(c));		\
        } while (0)
#else
#define stress_asm_x86_cpuid(a, b, c, d)		\
	do {						\
		__asm__ __volatile__ (			\
			"cpuid\n"			\
			: "=a"(a),			\
			  "=b"(b),			\
			  "=c"(c),			\
			  "=d"(d)			\
			: "0"(a),"2"(c));		\
	} while (0)
#endif

#if defined(HAVE_ASM_X86_PAUSE)
static inline void ALWAYS_INLINE stress_asm_x86_pause(void)
{
	__asm__ __volatile__("pause;\n" ::: "memory");
}
#endif

#if defined(HAVE_ASM_X86_SERIALIZE)
static inline void ALWAYS_INLINE stress_asm_x86_serialize(void)
{
	__asm__ __volatile__("serialize");
}
#endif

/*
 *  x86 read TSC
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdtsc(void)
{
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_RDTSC)
	uint32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
#else
	return 0;
#endif
}

/*
 *  x86 read TSCP
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdtscp(void)
{
#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_RDTSCP)
	uint32_t lo, hi, tsc_aux;

	__asm__ __volatile__("rdtscp" : "=a" (lo), "=d" (hi), "=c" (tsc_aux));
	return ((uint64_t)hi << 32) | lo;
#else
	return 0;
#endif
}

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_ASM_X86_RDRAND)
/*
 *  stress_asm_x86_rdrand()
 *	read 64 bit random value
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdrand(void)
{
	uint64_t        ret;

	__asm__ __volatile__(
	"1:;\n\
		rdrand %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	return ret;
}
#elif defined(STRESS_ARCH_X86_32) &&	\
      defined(HAVE_ASM_X86_RDRAND)
/*
 *  stress_asm_x86_rdrand()
 *	read 2 x 32 bit random value
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdrand(void)
{
	uint32_t ret;
	uint64_t ret64;

	__asm__ __volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	ret64 = (uint64_t)ret << 32;

	__asm__ __volatile__("1:;\n\
	rdrand %0;\n\
	jnc 1b;\n":"=r"(ret));

	return ret64 | ret;
}
#endif

#if defined(STRESS_ARCH_X86_64) &&	\
    defined(HAVE_ASM_X86_RDSEED)
/*
 *  stress_asm_x86_rdseed()
 *	read 64 bit random value
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdseed(void)
{
	uint64_t        ret;

	__asm__ __volatile__(
	"1:;\n\
		rdseed %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	return ret;
}
#elif defined(STRESS_ARCH_X86_32) &&	\
      defined(HAVE_ASM_X86_RDSEED)
/*
 *  stress_asm_x86_rdseed()
 *	read 2 x 32 bit random value
 */
static inline uint64_t ALWAYS_INLINE stress_asm_x86_rdseed(void)
{
	uint32_t ret;
	uint64_t ret64;

	__asm__ __volatile__(
	"1:;\n\
		rdseed %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	ret64 = (uint64_t)ret << 32;

	__asm__ __volatile__(
	"1:;\n\
		rdseed %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	return ret64 | ret;
}
#endif

/* #if defined(STRESS_ARCH_X86) */
#endif

#if defined(HAVE_ASM_X86_TPAUSE) && 	\
    !defined(HAVE_COMPILER_PCC)
static inline int ALWAYS_INLINE stress_asm_x86_tpause__(int state, uint32_t hi, uint32_t lo)
{
	uint8_t cflags;

	__asm__ __volatile__(
		"mov %1, %%edx;\n"
		"mov %2, %%eax;\n"
		"mov %3, %%edi;\n"
		".byte 0x66,0x0f,0xae,0xf7;\n"	/* tpause %%edi; */
		"setb %0;\n"
		: "=r" (cflags)
		: "r" (hi), "r" (lo), "r"(state)
		: "cc", "eax", "edx", "edi");
	return cflags;
}

static inline int ALWAYS_INLINE stress_asm_x86_tpause(const int state, const uint64_t delay)
{
	register uint32_t lo = delay & 0xffffffff;
	register uint32_t hi = (uint32_t)(delay >> 32);

	return stress_asm_x86_tpause__(state, hi, lo);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static inline void ALWAYS_INLINE stress_asm_x86_clflush(volatile void *p)
{
        __asm__ __volatile__("clflush (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
static inline void ALWAYS_INLINE stress_asm_x86_clflushopt(void *p)
{
        __asm__ __volatile__("clflushopt (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
static inline void ALWAYS_INLINE stress_asm_x86_cldemote(void *p)
{
        __asm__ __volatile__("cldemote (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_CLWB)
static inline void ALWAYS_INLINE stress_asm_x86_clwb(void *p)
{
        __asm__ __volatile__("clwb (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_LFENCE)
static inline void ALWAYS_INLINE stress_asm_x86_lfence(void)
{
	__asm__ __volatile__("lfence" : : : "memory");
}
#endif

#if defined(HAVE_ASM_X86_MFENCE)
static inline void ALWAYS_INLINE stress_asm_x86_mfence(void)
{
	__asm__ __volatile__("mfence" : : : "memory");
}
#endif

#if defined(HAVE_ASM_X86_SFENCE)
static inline void ALWAYS_INLINE stress_asm_x86_sfence(void)
{
	__asm__ __volatile__("sfence" : : : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT0)
static inline void ALWAYS_INLINE stress_asm_x86_prefetcht0(void *p)
{
	__asm__ __volatile__("prefetcht0 (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT1)
static inline void ALWAYS_INLINE stress_asm_x86_prefetcht1(void *p)
{
	__asm__ __volatile__("prefetcht1 (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT2)
static inline void ALWAYS_INLINE stress_asm_x86_prefetcht2(void *p)
{
	__asm__ __volatile__("prefetcht2 (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHNTA)
static inline void ALWAYS_INLINE stress_asm_x86_prefetchnta(void *p)
{
	__asm__ __volatile__("prefetchnta (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHW)
static inline void ALWAYS_INLINE stress_asm_x86_prefetchw(void *p)
{
	__asm__ __volatile__("prefetchw (%0)\n" : : "r"(p) : "memory");
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHWT1)
static inline void ALWAYS_INLINE stress_asm_x86_prefetchwt1(void *p)
{
	__asm__ __volatile__("prefetchwt1 (%0)\n" : : "r"(p) : "memory");
}
#endif

#if !defined(HAVE_COMPILER_PCC) && 	\
    defined(STRESS_ARCH_X86_64) && 	\
    defined(STRESS_ARCH_X86_LP64)
static inline int ALWAYS_INLINE stress_asm_x86_umwait__(int state, uint32_t hi, uint32_t lo)
{
	uint8_t cflags;

	__asm__ __volatile__(
		"mov %1, %%edx;\n"
		"mov %2, %%eax;\n"
		"mov %3, %%edi;\n"
		".byte 0xf2, 0x0f, 0xae, 0xf7;\n"	/* umwait %edi */
		"setb %0;\n"
		: "=r" (cflags)
		: "r" (hi), "r" (lo), "r"(state)
		: "cc", "eax", "edx", "edi");
	return cflags;
}

static inline int ALWAYS_INLINE stress_asm_x86_umwait(const int state, const uint64_t delay)
{
	register uint32_t lo = delay & 0xffffffff;
	register uint32_t hi = (uint32_t)(delay >> 32);

	return stress_asm_x86_umwait__(state, hi, lo);
}

static inline void ALWAYS_INLINE stress_asm_x86_umonitor(void *addr)
{
	__asm__ __volatile__("mov %0, %%rdi\t\n"
		     ".byte 0xf3, 0x0f, 0xae, 0xf7\t\n"
		     : : "r" (addr));
}
#endif

#if defined(HAVE_ASM_X86_MOVDIRI)
static inline void ALWAYS_INLINE stress_ds_store64(void *ptr, const uint64_t val)
{
        __asm__ __volatile__("movdiri %0, (%1)\n"
                        : :  "r" (val), "r" (ptr));
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_LAHF)
static inline uint8_t ALWAYS_INLINE stress_asm_lahf(void)
{
	uint8_t value;

	__asm__ __volatile__("lahf;\n"
			     "mov %%ah, %0;\n"
			     : "=r" (value)
			     :
			     : "ah");
	return value;
}
#endif

/* #ifndef CORE_ASM_X86_H */
#endif
