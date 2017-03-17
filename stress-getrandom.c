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

#if defined(__OpenBSD__) || \
    (defined(__linux__) && defined(__NR_getrandom))

/*
 *  stress_getrandom
 *	stress reading random values using getrandom()
 */
int stress_getrandom(const args_t *args)
{
	do {
#if defined(__OpenBSD__)
		char buffer[256];
#else
		char buffer[8192];
#endif
		ssize_t ret;

		ret = shim_getrandom(buffer, sizeof(buffer), 0);
		if (ret < 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				continue;
			pr_fail_err("getrandom");
			return EXIT_FAILURE;
		}
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
#else
int stress_getrandom(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
