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
#ifndef CORE_ASM_RISCV_H
#define CORE_ASM_RISCV_H

#include "core-arch.h"
#include "core-attribute.h"

#if defined(STRESS_ARCH_RISCV)
#define STRESS_ZICBOM_CBO_CLEAN	(1)
#define STRESS_ZICBOM_CBO_FLUSH	(2)
#define STRESS_ZICBOZ_CBO_ZERO	(4)

#define STRESS_CBO_RS1		(10)
#define STRESS_CBO_FUNCT3	(2)
#define STRESS_CBO_OPCODE	(15)


#if defined(__NR_riscv_hwprobe)
#include <asm/hwprobe.h>
#endif

#if defined(__BYTE_ORDER__) &&	\
    defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_BIG_ENDIAN__
#define __bswap32(x) ((uint32_t)__builtin_bswap32(x))
#else
#define __bswap32(x) (x)
#endif
#endif

#define MK_CBO(op) __bswap32((uint32_t)(op) << 20 |                             \
                             STRESS_CBO_RS1 << 15 |                             \
                          STRESS_CBO_FUNCT3 << 12 |                             \
                                           0 << 7 |                             \
                             STRESS_CBO_OPCODE     )

#define CBO_INSN(base, op)                                                      \
({                                                                              \
        __asm__ __volatile__(                                                   \
        "mv     a0, %0\n"                                                       \
        "li     a1, %1\n"                                                       \
        ".4byte %2\n"                                                           \
        : : "r" (base), "i" (op), "i" (MK_CBO(op)) : "a0", "a1", "memory");     \
})

static inline uint64_t ALWAYS_INLINE stress_asm_riscv_rdtime(void)
{
	register unsigned long int ticks;

        __asm__ __volatile__("rdtime %0"
                              : "=r" (ticks)
			      :
                              : "memory");
	return (uint64_t)ticks;
}

#if defined(HAVE_ASM_RISCV_FENCE)
static inline void ALWAYS_INLINE stress_asm_riscv_fence(void)
{
         __asm__ __volatile__("fence" ::: "memory");
}
#endif

/* Flush instruction cache */
#if defined(HAVE_ASM_RISCV_FENCE_I)
static inline void ALWAYS_INLINE stress_asm_riscv_fence_i(void)
{
         __asm__ __volatile__("fence.i" ::: "memory");
}
#endif

/* Pause instruction */
static inline void ALWAYS_INLINE stress_asm_riscv_pause(void)
{
	/* pause is encoded as a fence instruction with pred=W, succ=0, and fm=0 */
	__asm__ __volatile__ (".4byte 0x100000F");
}

/* cbo.zero instruction */
#if defined(HAVE_ASM_RISCV_CBO_ZERO)
static inline void ALWAYS_INLINE stress_asm_riscv_cbo_zero(char *addr)
{
        __asm__ __volatile__(
        "mv     a0, %0\n"
        "li     a1, %1\n"
        ".4byte %2\n"
        : : "r" (addr), "i" (STRESS_ZICBOZ_CBO_ZERO), "i" (MK_CBO(STRESS_ZICBOZ_CBO_ZERO)) : "a0", "a1", "memory");
}
#endif

/* cbo.zero instruction */
#if defined(HAVE_ASM_RISCV_CBO_CACHE_MANAGEMENT)
static inline void ALWAYS_INLINE stress_asm_riscv_cbo_flush(const void *addr)
{
	CBO_INSN(addr, STRESS_ZICBOM_CBO_FLUSH);
}

static inline void ALWAYS_INLINE stress_asm_riscv_cbo_clean(const void *addr)
{
	CBO_INSN(addr, STRESS_ZICBOM_CBO_CLEAN);
}
#endif

static inline bool ALWAYS_INLINE stress_asm_riscv_has_cbom(void)
{
#if defined(__NR_riscv_hwprobe) && \
	defined(RISCV_HWPROBE_EXT_ZICBOM)
	cpu_set_t cpus;
	struct riscv_hwprobe pair;

	(void)sched_getaffinity(0, sizeof(cpu_set_t), &cpus);
	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	if (syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0) != 0)
		return false;

	return !!(pair.value & RISCV_HWPROBE_EXT_ZICBOM);
#else
	return false;
#endif
}

static inline uint64_t ALWAYS_INLINE stress_asm_riscv_cl_size(void)
{
#if defined(__NR_riscv_hwprobe) && \
	defined(RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE)
	cpu_set_t cpus;
	struct riscv_hwprobe pair;

	(void)sched_getaffinity(0, sizeof(cpu_set_t), &cpus);
	pair.key = RISCV_HWPROBE_KEY_ZICBOM_BLOCK_SIZE;
	if (syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0) != 0)
		return 0u;

	return pair.value;
#else
	return 0u;
#endif

}

/* #if defined(STRESS_ARCH_RISCV) */
#endif

/* #ifndef CORE_ASM_RISCV_H */
#endif
