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

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
int stress_io(const args_t *args)
{
#if defined(__linux__)
	int fd;
#endif

#if defined(__linux__)
	fd = openat(AT_FDCWD, ".", O_RDONLY | O_NONBLOCK | O_DIRECTORY);
#endif

	do {
		sync();
#if defined(__linux__) && NEED_GLIBC(2,14,0)
		if ((fd != -1) && (syncfs(fd) < 0))
			pr_fail_err("syncfs");
#endif
		inc_counter(args);
	} while (keep_stressing());

#if defined(__linux__)
	if (fd != -1)
		(void)close(fd);
#endif

	return EXIT_SUCCESS;
}
