/*
 * Copyright (C) 2017-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-pragma.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"

#if defined(HAVE_ASM_X86_REP_STOSQ) &&  \
    !defined(__ILP32__)
#define USE_ASM_X86_REP_STOSQ
#endif

/*
 *  stress_mmap_set()
 *	set mmap'd data, touching pages in
 *	a specific pattern - check with
 *	mmap_check().
 */
void OPTIMIZE3 stress_mmap_set(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t val = stress_mwc64();
	register uint64_t *ptr = (uint64_t *)buf;
	register const uint64_t *end = (uint64_t *)(buf + sz);
#if defined(USE_ASM_X86_REP_STOSQ)
        register const uint32_t loops = page_size / sizeof(uint64_t);
#endif

	while (ptr < end) {
#if !defined(USE_ASM_X86_REP_STOSQ)
		register const uint64_t *page_end = (uint64_t *)((uintptr_t)ptr + page_size);
#endif

		if (!stress_continue_flag())
			break;

		/*
		 *  ..and fill a page with uint64_t values
		 */
#if defined(USE_ASM_X86_REP_STOSQ)
        __asm__ __volatile__(
                "mov %0,%%rax\n;"
                "mov %1,%%rdi\n;"
                "mov %2,%%ecx\n;"
                "rep stosq %%rax,%%es:(%%rdi);\n"
                :
                : "r" (val),
		  "r" (ptr),
                  "r" (loops)
                : "ecx","rdi","rax");
		ptr += loops;
#else
		page_end = (const uint64_t *)STRESS_MINIMUM(end, page_end);

		while (ptr < page_end) {
			ptr[0x00] = val;
			ptr[0x01] = val;
			ptr[0x02] = val;
			ptr[0x03] = val;
			ptr[0x04] = val;
			ptr[0x05] = val;
			ptr[0x06] = val;
			ptr[0x07] = val;
			ptr[0x08] = val;
			ptr[0x09] = val;
			ptr[0x0a] = val;
			ptr[0x0b] = val;
			ptr[0x0c] = val;
			ptr[0x0d] = val;
			ptr[0x0e] = val;
			ptr[0x0f] = val;
			ptr += 16;
		}
#endif
		val++;
	}
}

/*
 *  stress_mmap_check()
 *	check if mmap'd data is sane
 */
int OPTIMIZE3 stress_mmap_check(
	uint8_t *buf,
	const size_t sz,
	const size_t page_size)
{
	register uint64_t *ptr = (uint64_t *)buf;
	register const uint64_t *end = (uint64_t *)(buf + sz);
	register uint64_t val = *ptr;

	while ((ptr < end) && stress_continue_flag()) {
		register const uint64_t *page_end = (uint64_t *)((uintptr_t)ptr + page_size);

		while (ptr < page_end) {
			if (ptr[0x00] != val)
				return -1;
			if (ptr[0x01] != val)
				return -1;
			if (ptr[0x02] != val)
				return -1;
			if (ptr[0x03] != val)
				return -1;
			if (ptr[0x04] != val)
				return -1;
			if (ptr[0x05] != val)
				return -1;
			if (ptr[0x06] != val)
				return -1;
			if (ptr[0x07] != val)
				return -1;
			shim_builtin_prefetch(ptr + 64);
			if (ptr[0x08] != val)
				return -1;
			if (ptr[0x09] != val)
				return -1;
			if (ptr[0x0a] != val)
				return -1;
			if (ptr[0x0b] != val)
				return -1;
			if (ptr[0x0c] != val)
				return -1;
			if (ptr[0x0d] != val)
				return -1;
			if (ptr[0x0e] != val)
				return -1;
			if (ptr[0x0f] != val)
				return -1;
			shim_builtin_prefetch(ptr + 128);
			ptr += 16;
		}
		val++;
	}
	return 0;
}
