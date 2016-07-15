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
#include <errno.h>
#include <sys/mman.h>

#include "stress-ng.h"

#if defined(STRESS_ICACHE)

#define SIZE	(4096)

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
static void SECTION(stress_icache_callee) ALIGNED(SIZE) stress_icache_func(void)
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
int SECTION(stress_icache_caller) ALIGNED(SIZE) stress_icache(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	volatile uint8_t *addr = (uint8_t *)stress_icache_func;
	const size_t page_size = stress_get_pagesize();

	(void)instance;

	if (page_size != SIZE) {
		pr_inf(stderr, "%s: page size %zu is not %u, cannot test\n",
			name, page_size, SIZE);
		return EXIT_NO_RESOURCE;
	}
#if defined(MADV_NOHUGEPAGE)
	if (madvise((void *)addr, SIZE, MADV_NOHUGEPAGE) < 0) {
		/*
		 * We may get EINVAL on kernels that don't support this
		 * so don't treat that as non-fatal as this is just advistory
		 */
		if (errno != EINVAL) {
			pr_inf(stderr, "%s: madvise MADV_NOHUGEPAGE failed on text page %p: errno=%d (%s)\n",
				name, addr, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}
	}
#endif

	do {
		register uint8_t val;
		register int i = 1024;

		while (--i) {
			/*
			 *  Change protection to make page modifyable. It may be that
			 *  some architectures don't allow this, so don't bail out on
			 *  a EXIT_FAILURE; this is a not necessarily a fault in the
			 *  the stressor, just an arch resource protection issue.
			 */
			if (mprotect((void *)addr, SIZE, PROT_READ | PROT_WRITE) < 0) {
				pr_inf(stderr, "%s: PROT_WRITE mprotect failed on text page %p: errno=%d (%s)\n",
					name, addr, errno, strerror(errno));
				return EXIT_NO_RESOURCE;
			}
			/*
			 *  Modifying executable code on x86 will
			 *  call a I-cache reload when we execute
			 *  the modfied ops.
			 */
			val = *addr;
			*addr ^= ~0;

			/*
			 * ARM CPUs need us to clear the I$ between
			 * each modification of the object code.
			 *
			 * We may need to do the same for other processors
			 * as the default code assumes smart x86 style
			 * I$ behaviour.
			 */
#if defined(__GNUC__) && defined(STRESS_ARM)
			__clear_cache((char *)addr, (char *)addr + 64);
#endif
			*addr = val;
#if defined(__GNUC__) && defined(STRESS_ARM)
			__clear_cache((char *)addr, (char *)addr + 64);
#endif
			/*
			 *  Set back to a text segment READ/EXEC page attributes, this
			 *  really should not fail.
			 */
			if (mprotect((void *)addr, SIZE, PROT_READ | PROT_EXEC) < 0) {
				pr_err(stderr, "%s: mprotect failed: errno=%d (%s)\n",
					name, errno, strerror(errno));
				return EXIT_FAILURE;
			}

			stress_icache_func();
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
