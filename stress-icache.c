/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>

#include "stress-ng.h"

#if defined(STRESS_ICACHE)

// 4096 bytes should be enough for below proto type function
#define FUNC_SIZE 4096

uint8_t *stress_icache_func;

uint8_t stress_icache_proto_func_0(void)
{
	return 0;
}

uint8_t stress_icache_proto_func_1(void)
{
	return 1;
}

/*
 *  stress_icache()
 *	stress instruction cache load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
int stress_icache(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	uint32_t pagesize = getpagesize();

	uint8_t *ptr = malloc(FUNC_SIZE + pagesize + 1);
	if (NULL == ptr) {
		return EXIT_FAILURE;
	}

	//Align the address on a page boundary
	stress_icache_func = (uint8_t *)(((uint64_t)ptr + pagesize - 1) & ~(pagesize - 1));

	if (mprotect((void *)stress_icache_func, FUNC_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
		pr_err(stderr, "%s: PROT_WRITE mprotect failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	void* func_array[2] = {stress_icache_proto_func_0, stress_icache_proto_func_1};

	do {
		uint8_t val;

		memcpy(stress_icache_func, func_array[*counter & 0x1], FUNC_SIZE);

		__clear_cache((uint8_t*)stress_icache_func, (uint8_t*)stress_icache_func + FUNC_SIZE);

		val = ((uint8_t (*)())stress_icache_func)();
		if(val != (uint8_t)(*counter & 0x1)) {
			return EXIT_FAILURE;
		}

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (mprotect((void *)stress_icache_func, FUNC_SIZE, PROT_READ | PROT_EXEC) < 0) {
		pr_err(stderr, "%s: mprotect failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	free(ptr);

	return EXIT_SUCCESS;
}
#endif
