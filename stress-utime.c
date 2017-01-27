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
#include <utime.h>

/*
 *  stress_utime()
 *	stress system by setting file utime
 */
int stress_utime(args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, args->pid, args->instance, mwc32());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_err(stderr, "%s: open failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
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
				args->name, errno, strerror(errno));
			break;
		}
#if defined(__linux__)
		if (futimens(fd, NULL) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		ts.tv_sec = UTIME_NOW;
		ts.tv_nsec = UTIME_NOW;
		if (futimens(fd, &ts) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		ts.tv_sec = UTIME_OMIT;
		ts.tv_nsec = UTIME_OMIT;
		if (futimens(fd, &ts) < 0) {
			pr_dbg(stderr, "%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#endif
		if (utime(filename, NULL) < 0) {
			pr_dbg(stderr, "%s: utime failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		/* forces metadata writeback */
		if (opt_flags & OPT_FLAGS_UTIME_FSYNC)
			(void)fsync(fd);
		inc_counter(args);
	} while (opt_do_run && (!args->max_ops || *args->counter < args->max_ops));

	(void)close(fd);
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}
