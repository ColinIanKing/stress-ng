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
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <utime.h>


#include "stress-ng.h"

/*
 *  stress_utime()
 *	stress system by setting file utime
 */
int stress_utime(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char filename[PATH_MAX];
	int fd;
	const pid_t pid = getpid();

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		pr_err(stderr, "%s: open failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}

	do {
#if defined(__linux__)
		if (futimens(fd, NULL) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}
#endif
		if (utime(filename, NULL) < 0) {
			pr_dbg(stderr, "%s: utime failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}
		/* forces metadata writeback */
		if (opt_flags & OPT_FLAGS_UTIME_FSYNC)
			(void)fsync(fd);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	(void)close(fd);
	(void)unlink(filename);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}
