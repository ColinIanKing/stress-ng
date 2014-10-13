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
#include <inttypes.h>
#include <limits.h>
#include <unistd.h>

#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

/*
 *  stress_dentry_unlink()
 *	remove all dentries
 */
static void stress_dentry_unlink(uint64_t n)
{
	uint64_t i;
	const pid_t pid = getpid();

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		snprintf(path, sizeof(path), "stress-dentry-%i-%"
			PRIu64 ".tmp", pid, gray_code);
		(void)unlink(path);
	}
	sync();
}

/*
 *  stress_dentry
 *	stress dentries
 */
int stress_dentry(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();

	(void)instance;

	do {
		uint64_t i, n = opt_dentries;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;
			int fd;

			snprintf(path, sizeof(path), "stress-dentry-%i-%"
				PRIu64 ".tmp", pid, gray_code);

			if ((fd = open(path, O_CREAT | O_RDWR, 0666)) < 0) {
				pr_failed_err(name, "open");
				n = i;
				break;
			}
			close(fd);

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_dentry_unlink(n);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_dbg(stdout, "%s: removing %" PRIu64 " entries\n", name, opt_dentries);
	stress_dentry_unlink(opt_dentries);

	return EXIT_SUCCESS;
}
