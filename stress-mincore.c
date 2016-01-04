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

#include "stress-ng.h"

#if defined(STRESS_MINCORE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define VEC_MAX_SIZE 	(64)

/*
 *  stress_mincore()
 *	stress mincore system call
 */
int stress_mincore(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	volatile uint8_t *addr = 0;
	const size_t page_size = stress_get_pagesize();
	const ptrdiff_t mask = ~(page_size - 1);

	(void)instance;

	do {
		int i;

		for (i = 0; (i < 100) && opt_do_run; i++) {
			int ret, redo = 0;
			unsigned char vec[1];

			if (opt_flags & OPT_FLAGS_MINCORE_RAND)
				if (addr < (uint8_t *)page_size)
					addr = (uint8_t *)((ptrdiff_t)(mwc64() & mask));
redo:
			errno = 0;
			ret = mincore((void *)addr, page_size, vec);
			if (ret < 0) {
				switch (errno) {
				case ENOMEM:
					/* Page not mapped */
					break;
				case EAGAIN:
					if (++redo < 100)
						goto redo;
					/* fall through */
				default:
					pr_fail(stderr, "%s: mincore on address %p error: %d %s\n ",
						name, addr, errno, strerror(errno));
					return EXIT_FAILURE;
				}
			}
			if (opt_flags & OPT_FLAGS_MINCORE_RAND)
				addr = (uint8_t *)(ptrdiff_t)
					(((ptrdiff_t)addr >> 1) & mask);
			else
				addr += page_size;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}

#endif
