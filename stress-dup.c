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
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

/*
 *  stress_dup()
 *	stress system by rapid dup/close calls
 */
int stress_dup(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fds[STRESS_FD_MAX];
	const size_t max_fd = stress_get_file_limit();
	size_t i;
#if defined(__linux__)
	bool do_dup3 = true;
#endif

	(void)instance;

	fds[0] = open("/dev/zero", O_RDONLY);
	if (fds[0] < 0) {
		pr_fail_dbg(name, "open on /dev/zero");
		return EXIT_FAILURE;
	}

	do {
		size_t n;

		for (n = 1; n < max_fd; n++) {
			int tmp;

			fds[n] = dup(fds[0]);
			if (fds[n] < 0)
				break;
#if defined(__linux__)
			if (do_dup3 && (mwc32() & 1)) {
				int fd;

				fd = dup3(fds[0], fds[n], O_CLOEXEC);
				/* No dup3 support? then fallback to dup2 */
				if ((fd < 0) && (errno == ENOSYS)) {
					fd = dup2(fds[0], fds[n]);
					do_dup3 = false;
				}
				fds[n] = fd;
			} else {
				fds[n] = dup2(fds[0], fds[n]);
			}
#else
			fds[n] = dup2(fds[0], fds[n]);
#endif
			if (fds[n] < 0)
				break;

			/* dup2 on the same fd should be a no-op */
			tmp = dup2(fds[n], fds[n]);
			if (tmp != fds[n]) {
				pr_fail_err(name, "dup2 with same fds");
				break;
			}

			if (!opt_do_run)
				break;
			(*counter)++;
		}
		for (i = 1; i < n; i++) {
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(void)close(fds[i]);
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	(void)close(fds[0]);

	return EXIT_SUCCESS;
}
