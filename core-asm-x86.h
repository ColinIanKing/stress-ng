/*
 * Copyright (C) 2023      Colin Ian King.
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

#include "stress-ng.h"
#include "core-arch.h"

#if defined(STRESS_ARCH_X86)
#define stress_asm_x86_lock_add(ptr, inc) 		\
	do {						\
		__asm__ __volatile__(			\
			"lock addl %1,%0"		\
			: "+m" (*ptr)			\
			: "ir" (inc));			\
	} while (0)
#endif

#if defined(STRESS_ARCH_X86)
#if defined(STRESS_ARCH_X86_32) && !NEED_GNUC(5, 0, 0) && defined(__PIC__)
#define stress_asm_x86_cpuid(a, b, c, d)		\
	do {						\
		__asm__ __volatile__ (			\
			"pushl %%ebx\n"			\
			"cpuid\n"			\
			"mov %%ebx,%1\n"		\
			"popl %%ebx\n"			\
			: "=a"(a),			\
			  "=r"(b),			\
			  "=r"(c),			\
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
#else
#define stress_asm_x86_cpuid(a, b, c, d)		\
	do {						\
		a = 0;					\
		b = 0;					\
		c = 0;					\
		d = 0;					\
	} while (0)
#endif

#if defined(HAVE_ASM_X86_PAUSE)
static inline void stress_asm_x86_pause(void)
{
	__asm__ __volatile__("pause;\n" ::: "memory");
}
#endif

#if defined(HAVE_ASM_X86_TPAUSE)
static inline void stress_asm_x86_tpause(uint32_t ecx, uint32_t delay)
{
	uint32_t lo, hi;
	uint64_t val;

	__asm__ __volatile__("rdtsc\n" : "=a"(lo),"=d"(hi));
	val = (((uint64_t)hi << 32) | lo) + delay;
	lo = val & 0xffffffff;
	hi = val >> 32;
	__asm__ __volatile__("tpause %%ecx\n" :: "c"(ecx), "d"(hi), "a"(lo));
}
#endif

#if defined(HAVE_ASM_X86_SERIALIZE)
static inline void stress_asm_x86_serialize(void)
{
	__asm__ __volatile__("serialize");
}
#endif

/*
 *  x86 read TSC
 */
static inline uint64_t stress_asm_x86_rdtsc(void)
{
#if defined(STRESS_TSC_SERIALIZED)
	__asm__ __volatile__("cpuid\n");
#endif
#if defined(STRESS_ARCH_X86_64)
	uint32_t lo, hi;

	__asm__ __volatile__("rdtsc" : "=a" (lo), "=d" (hi));
	return ((uint64_t)hi << 32) | lo;
#endif
#if defined(STRESS_ARCH_X86_32)
	uint64_t tsc;

	__asm__ __volatile__("rdtsc" : "=A" (tsc));
	return tsc;
#endif
	return 0;
}

#endif

#if defined(STRESS_ARCH_X86_64)
/*
 *  stress_asm_x86_rdrand()
 *	read 64 bit random value
 */
static inline uint64_t stress_asm_x86_rdrand(void)
{
	uint64_t        ret;

	__asm__ __volatile__(
	"1:;\n\
		rdrand %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	return ret;
}

/*
 *  stress_asm_x86_rdseed()
 *	read 64 bit random value
 */
static inline uint64_t stress_asm_x86_rdseed(void)
{
	uint64_t        ret;

	__asm__ __volatile__(
	"1:;\n\
		rdseed %0;\n\
		jnc 1b;\n"
	: "=r"(ret));

	return ret;
}
#elif defined(STRESS_ARCH_X86_32)
/*
 *  stress_asm_x86_rdrand()
 *	read 2 x 32 bit random value
 */
static inline uint64_t stress_asm_x86_rdrand(void)
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

/*
 *  stress_asm_x86_rdseed()
 *	read 2 x 32 bit random value
 */
static inline uint64_t stress_asm_x86_rdseed(void)
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
