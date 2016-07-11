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

#if defined(STRESS_COPY_FILE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

static uint64_t opt_copy_file_bytes = DEFAULT_COPY_FILE_BYTES;
static bool set_copy_file_bytes;

static int sys_copy_file_range(
	int fd_in,
	loff_t *off_in,
	int fd_out,
	loff_t *off_out,
	size_t len,
	unsigned int flags)
{
#if defined(__NR_copy_file_range)
	return syscall(__NR_copy_file_range,
		fd_in, off_in, fd_out, off_out, len, flags);
#else
	(void)fd_in;
	(void)off_in;
	(void)fd_out;
	(void)off_out;
	(void)len;
	(void)flags;

	errno = ENOSYS;
	return -1;
#endif
}


void stress_set_copy_file_bytes(const char *optarg)
{
	set_copy_file_bytes = true;
	opt_copy_file_bytes =  get_uint64_byte(optarg);
	check_range("copy-file-bytes", opt_copy_file_bytes,
		MIN_COPY_FILE_BYTES, MAX_COPY_FILE_BYTES);
}

/*
 *  stress_copy_file
 *	stress reading chunks of file using copy_file_range()
 */
int stress_copy_file(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd_in, fd_out, rc = EXIT_FAILURE;
	char filename[PATH_MAX], tmp[PATH_MAX];
	pid_t pid = getpid();

	if (!set_copy_file_bytes) {
		if (opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_copy_file_bytes = MAX_HDD_BYTES;
		if (opt_flags & OPT_FLAGS_MINIMIZE)
			opt_copy_file_bytes = MIN_HDD_BYTES;
	}

	if (opt_copy_file_bytes < DEFAULT_COPY_FILE_SIZE)
		opt_copy_file_bytes = DEFAULT_COPY_FILE_SIZE * 2;

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		goto tidy_dir;
	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	snprintf(tmp, sizeof(tmp), "%s-orig", filename);
	if ((fd_in = open(tmp, O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto tidy_dir;
	}
	(void)unlink(tmp);
	if (ftruncate(fd_in, opt_copy_file_bytes) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "ftruncate");
		goto tidy_in;
	}
	if (fsync(fd_in) < 0) {
		pr_fail_err(name, "fsync");
		goto tidy_in;
	}

	snprintf(tmp, sizeof(tmp), "%s-copy", filename);
	if ((fd_out = open(tmp, O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto tidy_in;
	}
	(void)unlink(tmp);

	do {
		ssize_t ret;
		loff_t off_in, off_out;

		off_in = mwc64() % (opt_copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off_out = mwc64() % (opt_copy_file_bytes - DEFAULT_COPY_FILE_SIZE);

		ret =  sys_copy_file_range(fd_in, &off_in, fd_out, &off_out, DEFAULT_COPY_FILE_SIZE, 0);
		if (ret < 0) {
			if ((errno == EAGAIN) ||
			    (errno == EINTR) ||
			    (errno == ENOSPC))
				continue;
			pr_fail_err(name, "copy_file_range");
			goto tidy_out;
		}
		(void)fsync(fd_out);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
	rc = EXIT_SUCCESS;

tidy_out:
	(void)close(fd_out);
tidy_in:
	(void)close(fd_in);
tidy_dir:
	(void)stress_temp_dir_rm(name, pid, instance);

	return rc;
}

#endif
