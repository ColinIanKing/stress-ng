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
#include <inttypes.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

#if _XOPEN_SOURCE >= 600 || _POSIX_C_SOURCE >= 200112L
/*
 *  stress_fallocate
 *	stress I/O via fallocate and ftruncate
 */
int stress_fallocate(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	int fd;
	char filename[PATH_MAX];
	uint64_t ftrunc_errs = 0;

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}
	(void)unlink(filename);

	do {
		(void)posix_fallocate(fd, (off_t)0, 4096 * 4096);
		if (!opt_do_run)
			break;
		fsync(fd);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail(stderr, "fstat on file failed");
			else if (buf.st_size != (off_t)4096 * 4096)
					pr_fail(stderr, "file size does not match size the expected file size\n");
		}

		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!opt_do_run)
			break;
		fsync(fd);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail(stderr, "fstat on file failed");
			else if (buf.st_size != (off_t)0)
					pr_fail(stderr, "file size does not match size the expected file size\n");
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	if (ftrunc_errs)
		pr_dbg(stderr, "%s: %" PRIu64
			" ftruncate errors occurred.\n", name, ftrunc_errs);
	(void)close(fd);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}
#endif
