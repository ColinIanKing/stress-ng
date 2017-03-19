/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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
#include "stress-ng.h"

#if !defined(__gnu_hurd__) && NEED_GLIBC(2,2,0)

#define VEC_MAX_SIZE 	(64)

/*
 *  stress_mincore()
 *	stress mincore system call
 */
int stress_mincore(const args_t *args)
{
	uint8_t *addr = 0;
	const size_t page_size = args->page_size;
	const ptrdiff_t mask = ~(page_size - 1);

	do {
		int i;

		for (i = 0; (i < 100) && g_keep_stressing_flag; i++) {
			int ret, redo = 0;
			unsigned char vec[1];

			if (g_opt_flags & OPT_FLAGS_MINCORE_RAND)
				if (addr < (uint8_t *)page_size)
					addr = (uint8_t *)((ptrdiff_t)(mwc64() & mask));
redo:
			errno = 0;
			ret = shim_mincore((void *)addr, page_size, vec);
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
					pr_fail("%s: mincore on address %p error: %d %s\n ",
						args->name, addr, errno,
						strerror(errno));
					return EXIT_FAILURE;
				}
			}
			if (g_opt_flags & OPT_FLAGS_MINCORE_RAND)
				addr = (uint8_t *)(ptrdiff_t)
					(((ptrdiff_t)addr >> 1) & mask);
			else
				addr += page_size;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_mincore(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
