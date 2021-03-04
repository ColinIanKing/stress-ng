/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"copy-file N",		"start N workers that copy file data" },
	{ NULL,	"copy-file-ops N",	"stop after N copy bogo operations" },
	{ NULL,	"copy-file-bytes N",	"specify size of file to be copied" },
	{ NULL,	NULL,			NULL }

};

static int stress_set_copy_file_bytes(const char *opt)
{
	uint64_t copy_file_bytes;

	copy_file_bytes = stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("copy-file-bytes", copy_file_bytes,
		MIN_COPY_FILE_BYTES, MAX_COPY_FILE_BYTES);
	return stress_set_setting("copy-file-bytes", TYPE_ID_UINT64, &copy_file_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_copy_file_bytes,	stress_set_copy_file_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_COPY_FILE_RANGE)

/*
 *  stress_copy_file
 *	stress reading chunks of file using copy_file_range()
 */
static int stress_copy_file(const stress_args_t *args)
{
	int fd_in, fd_out, rc = EXIT_FAILURE;
	char filename[PATH_MAX - 5], tmp[PATH_MAX];
	uint64_t copy_file_bytes = DEFAULT_COPY_FILE_BYTES;

	if (!stress_get_setting("copy-file-bytes", &copy_file_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			copy_file_bytes = MAX_HDD_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			copy_file_bytes = MIN_HDD_BYTES;
	}

	copy_file_bytes /= args->num_instances;
	if (copy_file_bytes < DEFAULT_COPY_FILE_SIZE)
		copy_file_bytes = DEFAULT_COPY_FILE_SIZE * 2;
	if (copy_file_bytes < MIN_COPY_FILE_BYTES)
		copy_file_bytes = MIN_COPY_FILE_BYTES;

	if (stress_temp_dir_mk(args->name, args->pid, args->instance) < 0)
		goto tidy_dir;
	(void)stress_temp_filename_args(args,
			filename, sizeof(filename), stress_mwc32());
	(void)snprintf(tmp, sizeof(tmp), "%s-orig", filename);
	if ((fd_in = open(tmp, O_CREAT | O_RDWR,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, tmp, errno, strerror(errno));
		goto tidy_dir;
	}
	(void)unlink(tmp);
	if (ftruncate(fd_in, copy_file_bytes) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: ftruncated failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy_in;
	}
	if (shim_fsync(fd_in) < 0) {
		pr_fail("%s: fsync failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		goto tidy_in;
	}

	(void)snprintf(tmp, sizeof(tmp), "%s-copy", filename);
	if ((fd_out = open(tmp, O_CREAT | O_WRONLY,  S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, tmp, errno, strerror(errno));
		goto tidy_in;
	}
	(void)unlink(tmp);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		ssize_t ret;
		shim_loff_t off_in, off_out;

		off_in = stress_mwc64() % (copy_file_bytes - DEFAULT_COPY_FILE_SIZE);
		off_out = stress_mwc64() % (copy_file_bytes - DEFAULT_COPY_FILE_SIZE);

		ret = shim_copy_file_range(fd_in, &off_in, fd_out,
			&off_out, DEFAULT_COPY_FILE_SIZE, 0);
		if (ret < 0) {
			if ((errno == EAGAIN) ||
			    (errno == EINTR) ||
			    (errno == ENOSPC))
				continue;
			pr_fail("%s: copy_file_range failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy_out;
		}
		(void)shim_fsync(fd_out);
		inc_counter(args);
	} while (keep_stressing(args));
	rc = EXIT_SUCCESS;

tidy_out:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_out);
tidy_in:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd_in);
tidy_dir:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_copy_file_info = {
	.stressor = stress_copy_file,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_copy_file_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
