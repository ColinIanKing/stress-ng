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
#include <limits.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
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
	int ret, fd;
	const pid_t pid = getpid();

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_err(stderr, "%s: open failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		(void)stress_temp_dir_rm(name, pid, instance);
		return ret;
	}

	do {
		struct timeval times[2], *t;
#if defined(__linux__)
		struct timespec ts;
#endif

		if (gettimeofday(&times[0], NULL) < 0) {
			t = NULL;
		} else {
			times[1] = times[0];
			t = times;
		}
		if (utimes(filename, t) < 0) {
			pr_dbg(stderr, "%s: utimes failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}
#if defined(__linux__)
		if (futimens(fd, NULL) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}

		ts.tv_sec = UTIME_NOW;
		ts.tv_nsec = UTIME_NOW;
		if (futimens(fd, &ts) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				name, errno, strerror(errno));
			break;
		}

		ts.tv_sec = UTIME_OMIT;
		ts.tv_nsec = UTIME_OMIT;
		if (futimens(fd, &ts) < 0) {
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
