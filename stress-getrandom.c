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

#if defined(STRESS_GETRANDOM)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

/*
 *  getrandom() syscall
 */
static inline int sys_getrandom(void *buff, size_t buflen, unsigned int flags)
{
#if defined(__NR_getrandom)
	return syscall(__NR_getrandom, buff, buflen, flags);
#else
	errno = ENOSYS;
	return -1;
#endif
}

/*
 *  stress_getrandom
 *	stress reading random values using getrandom()
 */
int stress_getrandom(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;

	do {
		char buffer[8192];
		ssize_t ret;

		ret = sys_getrandom(buffer, sizeof(buffer), 0);
		if (ret < 0) {
			if (errno == EAGAIN)
				continue;
			pr_fail_err(name, "getrandom");
			return EXIT_FAILURE;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
