/*
 * Copyright (C) 2024-2025 Woodrow Shen
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
#include <stdint.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>
#if defined(__riscv) || \
    defined(__riscv__)
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

#define MK_CBO(op) __bswap32((uint32_t)(op) << 20 | 10 << 15 | 2 << 12 | 0 << 7 | 15)

#define CBO_INSN(base, op)                                                      \
({                                                                              \
        asm volatile(                                                           \
        "mv     a0, %0\n"                                                       \
        "li     a1, %1\n"                                                       \
        ".4byte %2\n"                                                           \
        : : "r" (base), "i" (op), "i" (MK_CBO(op)) : "a0", "a1", "memory");   \
})

static void cbo_zero(char *base)  { CBO_INSN(base, 4); }

static char mem[4096] __attribute__((aligned(4096))) = { [0 ... 4095] = 0xaa };

#if defined(__riscv) || \
    defined(__riscv__)
int main(void)
{
#if defined(HAVE_SYSCALL) &&            \
    defined(__NR_riscv_hwprobe)
	int ret;
        struct riscv_hwprobe pair;
        cpu_set_t cpus;

        ret = sched_getaffinity(0, sizeof(cpu_set_t), &cpus);

        pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
        ret = (int)syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0);

        if (pair.value & RISCV_HWPROBE_EXT_ZICBOZ) {
                uint64_t block_size;
                pair.key = RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE;

                ret = (int)syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0);
                block_size = pair.value;

                for (int i = 0; i < 4096 / block_size; ++i) {
                    cbo_zero(&mem[i * block_size]);
                }
        }
#endif
	return 0;
}
#else
#error not RISC-V so no cbo.zero instruction
#endif
