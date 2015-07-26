/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#include <errno.h>
#include <sys/mman.h>

#include "stress-ng.h"

#if defined(STRESS_ICACHE)

#if defined(__GNUC__) && NEED_GNUC(4,6,0)
#define SECTION(s) __attribute__((__section__(# s)))
#define ALIGNED(a) __attribute__((aligned(a)))
#endif

/*
 *  stress_icache_func()
 *	page aligned in its own section so we can change the
 * 	code mapping and make it modifyable to force I-cache
 *	refreshes by modifying the code
 */
static void SECTION(stress_icache_callee) ALIGNED(4096) stress_icache_func(void)
{
	return;
}

/*
 *  stress_icache()
 *	stress instruction cache load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
int SECTION(stress_icache_caller) stress_icache(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	volatile uint8_t *addr = (uint8_t *)stress_icache_func;

	(void)instance;

	if (mprotect((uint8_t *)addr, 4096, PROT_READ | PROT_WRITE | PROT_EXEC) < 0) {
		pr_err(stderr, "%s: PROT_WRITE mprotect failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	do {
		register uint8_t val;
		register int i = 1024;

		while (--i) {
			/*
			 *   Modifying executable code on x86 will
			 *   call a I-cache reload when we execute
			 *   the modfied ops.
			 */
			val = *addr;
			*addr ^= ~0;
			*addr = val;
			stress_icache_func();
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	if (mprotect((uint8_t *)addr, 4096, PROT_READ | PROT_EXEC) < 0) {
		pr_err(stderr, "%s: mprotect failed: errno=%d (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}
#endif
