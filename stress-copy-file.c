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

static uint64_t opt_copy_file_bytes = DEFAULT_COPY_FILE_BYTES;
static bool set_copy_file_bytes;

void stress_set_copy_file_bytes(const char *optarg)
{
	set_copy_file_bytes = true;
	opt_copy_file_bytes =
		get_uint64_byte_filesystem(optarg,
			stressor_instances(STRESS_COPY_FILE));
	check_range_bytes("copy-file-bytes", opt_copy_file_bytes,
		MIN_COPY_FILE_BYTES, MAX_COPY_FILE_BYTES);
}

#if defined(__linux__) && (__NR_copy_file_range)

/*
 *  stress_copy_file
 *	stress reading chunks of file using copy_file_range()
 */
int stress_copy_file(const args_t *args)
{
	int fd_in, fd_out, rc = EXIT_FAILURE;
	char filename[PATH_MAX], tmp[PATH_MAX];

	if (!set_copy_file_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_copy_file_bytes = MAX_HDD_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_copy_file_bytes = MIN_HDD_BYTES;
	}

	if (opt_copy_file_bytes < DEFAULT_COPY_FILE_SIZE)
		opt_copy_file_bytes = DEFAULT_COPY_FILE_SIZE * 2;

	if (stress_temp_dir_mk(args->name, args->pid, args->instance) < 0)
		goto tidy_dir;
	(void)stress_temp_filename_args(args,
			filename, sizeof(filename), mwc32());
	(void)snprintf(tmp, sizeof(tmp), "%s-orig", filename);
	if ((fd_in = open(tmp, O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto tidy_dir;
	}
	(void)unlink(tmp);
	if (ftruncate(fd_in, opt_copy_file_bytes) < 0) {
		rc = exit_status(errno);
		pr_fail_err("ftruncate");
		goto tidy_in;
	}
	if (fsync(fd_in) < 0) {
		pr_fail_err("fsync");
		goto tidy_in;
	}

	(void)snprintf(tmp, sizeof(tmp), "%s-copy", filename);
	if ((fd_out = open(tmp, O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto tidy_in;
	}
	(void)unlink(tmp);

	do {
		ssize_t ret;
		loff_t off_in, off_out;

		off_in = mwc64() % (opt_copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off_out = mwc64() % (opt_copy_file_bytes - DEFAULT_COPY_FILE_SIZE);

		ret =  shim_copy_file_range(fd_in, &off_in, fd_out,
			&off_out, DEFAULT_COPY_FILE_SIZE, 0);
		if (ret < 0) {
			if ((errno == EAGAIN) ||
			    (errno == EINTR) ||
			    (errno == ENOSPC))
				continue;
			pr_fail_err("copy_file_range");
			goto tidy_out;
		}
		(void)fsync(fd_out);
		inc_counter(args);
	} while (keep_stressing());
	rc = EXIT_SUCCESS;

tidy_out:
	(void)close(fd_out);
tidy_in:
	(void)close(fd_in);
tidy_dir:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}
#else
int stress_copy_file(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
