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
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/resource.h>

#include "stress-ng.h"

/*
 *  stress_open()
 *	stress system by rapid open/close calls
 */
int stress_open(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fds[STRESS_FD_MAX];
	struct rlimit rlim;
	rlim_t i, opened = 0;

	(void)instance;
	(void)name;

	if (getrlimit(RLIMIT_NOFILE, &rlim) < 0)
		rlim.rlim_cur = STRESS_FD_MAX;	/* Guess */

	/* Determine max number of free file descriptors we have */
	for (i = 0; i < rlim.rlim_cur; i++) {
		if (fcntl((int)i, F_GETFL) > -1)
			opened++;
	}
	rlim.rlim_cur -= opened;

	do {
		for (i = 0; i < rlim.rlim_cur; i++) {
			fds[i] = open("/dev/zero", O_RDONLY);
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(*counter)++;
		}
		for (i = 0; i < rlim.rlim_cur; i++) {
			if (fds[i] < 0)
				break;
			if (!opt_do_run)
				break;
			(void)close(fds[i]);
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
