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

#if defined(STRESS_FALLOCATE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <inttypes.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

static off_t opt_fallocate_bytes = DEFAULT_FALLOCATE_BYTES;
static bool set_fallocate_bytes = false;

void stress_set_fallocate_bytes(const char *optarg)
{
	set_fallocate_bytes = true;
	opt_fallocate_bytes = (off_t)get_uint64_byte(optarg);
	check_range("fallocate-bytes", opt_fallocate_bytes,
		MIN_FALLOCATE_BYTES, MAX_FALLOCATE_BYTES);
}

#if defined(__linux__)
static const int modes[] = {
	0,
#if defined(FALLOC_FL_KEEP_SIZE)
	FALLOC_FL_KEEP_SIZE,
#endif
#if defined(FALLOC_FL_KEEP_SIZE) && defined(FALLOC_FL_PUNCH_HOLE)
	FALLOC_FL_KEEP_SIZE | FALLOC_FL_PUNCH_HOLE,
#endif
#if defined(FALLOC_FL_ZERO_RANGE)
	FALLOC_FL_ZERO_RANGE,
#endif
#if defined(FALLOC_FL_COLLAPSE_RANGE)
	FALLOC_FL_COLLAPSE_RANGE,
#endif
#if defined(FALLOC_FL_INSERT_RANGE)
	FALLOC_FL_INSERT_RANGE,
#endif
};
#endif

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
	int fd, ret;
	char filename[PATH_MAX];
	uint64_t ftrunc_errs = 0;

	if (!set_fallocate_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_fallocate_bytes = MAX_FALLOCATE_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_fallocate_bytes = MIN_FALLOCATE_BYTES;
	}

	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err(name, "open");
		(void)stress_temp_dir_rm(name, pid, instance);
		return ret;
	}
	(void)unlink(filename);

	do {
		(void)posix_fallocate(fd, (off_t)0, opt_fallocate_bytes);
		if (!opt_do_run)
			break;
		(void)fsync(fd);
		if (opt_flags & OPT_FLAGS_VERIFY) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail(stderr, "%s: fstat on file failed", name);
			else if (buf.st_size != opt_fallocate_bytes)
				pr_fail(stderr, "%s: file size %jd does not match size the expected file size of %jd\n",
					name, (intmax_t)buf.st_size,
					(intmax_t)opt_fallocate_bytes);
		}

		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		if (!opt_do_run)
			break;
		(void)fsync(fd);

		if (opt_flags & OPT_FLAGS_VERIFY) {
			struct stat buf;

			if (fstat(fd, &buf) < 0)
				pr_fail(stderr, "%s: fstat on file failed", name);
			else if (buf.st_size != (off_t)0)
				pr_fail(stderr, "%s: file size %jd does not match size the expected file size of 0\n",
					name, (intmax_t)buf.st_size);

		}

		if (ftruncate(fd, opt_fallocate_bytes) < 0)
			ftrunc_errs++;
		(void)fsync(fd);
		if (ftruncate(fd, 0) < 0)
			ftrunc_errs++;
		(void)fsync(fd);

#if defined(__linux__)
		if (SIZEOF_ARRAY(modes) > 1) {
			/*
			 *  non-portable Linux fallocate()
			 */
			int i;
			(void)fallocate(fd, 0, (off_t)0, opt_fallocate_bytes);
			if (!opt_do_run)
				break;
			(void)fsync(fd);

			for (i = 0; i < 64; i++) {
				off_t offset = (mwc64() % opt_fallocate_bytes) & ~0xfff;
				int j = (mwc32() >> 8) % SIZEOF_ARRAY(modes);

				(void)fallocate(fd, modes[j], offset, 64 * KB);
				if (!opt_do_run)
					break;
				(void)fsync(fd);
			}
			if (ftruncate(fd, 0) < 0)
				ftrunc_errs++;
			(void)fsync(fd);
		}
#endif
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
