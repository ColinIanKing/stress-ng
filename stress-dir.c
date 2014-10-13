/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "stress-ng.h"

/*
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(const uint64_t n)
{
	uint64_t i;
	const pid_t pid = getpid();

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		snprintf(path, sizeof(path), "stress-dir-%i-%"
			PRIu64, pid, gray_code);
		(void)rmdir(path);
	}
}

/*
 *  stress_dir
 *	stress directory mkdir and rmdir
 */
int stress_dir(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();

	(void)instance;

	do {
		uint64_t i, n = DEFAULT_DIRS;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;

			snprintf(path, sizeof(path), "stress-dir-%i-%"
				PRIu64, pid, gray_code);
			if (mkdir(path, 0666) < 0) {
				pr_failed_err(name, "mkdir");
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_dir_tidy(n);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu32 " directories\n", name, DEFAULT_DIRS);
	stress_dir_tidy(DEFAULT_DIRS);

	return EXIT_SUCCESS;
}
