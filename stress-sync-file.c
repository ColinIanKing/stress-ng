/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"sync-file N",	     "start N workers exercise sync_file_range" },
	{ NULL,	"sync-file-ops N",   "stop after N sync_file_range bogo operations" },
	{ NULL,	"sync-file-bytes N", "size of file to be sync'd" },
	{ NULL,	NULL,		     NULL }
};

#if defined(HAVE_SYNC_FILE_RANGE)
static const int sync_modes[] = {
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
	SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE,
	SYNC_FILE_RANGE_WAIT_AFTER,
	0	/* No-op */
};
#endif

static int stress_set_sync_file_bytes(const char *opt)
{
	off_t sync_file_bytes;

	sync_file_bytes = (off_t)get_uint64_byte_filesystem(opt, 1);
	check_range_bytes("sync_file-bytes", sync_file_bytes,
		MIN_SYNC_FILE_BYTES, MAX_SYNC_FILE_BYTES);
	return set_setting("sync_file-bytes", TYPE_ID_OFF_T, &sync_file_bytes);
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_sync_file_bytes,	stress_set_sync_file_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_SYNC_FILE_RANGE)

/*
 *  shrink and re-allocate the file to be sync'd
 *
 */
static int stress_sync_allocate(
	const args_t *args,
	const int fd,
	const off_t sync_file_bytes)
{
	int ret;

	ret = ftruncate(fd, 0);
	if (ret < 0) {
		pr_err("%s: ftruncate failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -errno;
	}

#if defined(HAVE_FDATASYNC)
	ret = shim_fdatasync(fd);
	if (ret < 0) {
		pr_err("%s: fdatasync failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -errno;
	}
#endif

	ret = shim_fallocate(fd, 0, (off_t)0, sync_file_bytes);
	if (ret < 0) {
		pr_err("%s: fallocate failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return -errno;
	}
	return 0;
}

/*
 *  stress_sync_file
 *	stress the sync_file_range system call
 */
static int stress_sync_file(const args_t *args)
{
	int fd, ret;
	off_t sync_file_bytes = DEFAULT_SYNC_FILE_BYTES;
	char filename[PATH_MAX];

	if (!get_setting("sync_file-bytes", &sync_file_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sync_file_bytes = MAX_SYNC_FILE_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sync_file_bytes = MIN_SYNC_FILE_BYTES;
	}
	sync_file_bytes /= args->num_instances;
	if (sync_file_bytes < (off_t)MIN_SYNC_FILE_BYTES)
		sync_file_bytes = (off_t)MIN_SYNC_FILE_BYTES;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err("open");
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	(void)unlink(filename);

	do {
		shim_off64_t i, offset;
		const size_t mode_index = mwc32() % SIZEOF_ARRAY(sync_modes);
		const int mode = sync_modes[mode_index];

		if (stress_sync_allocate(args, fd, sync_file_bytes) < 0)
			break;
		for (offset = 0; g_keep_stressing_flag &&
		     (offset < (shim_off64_t)sync_file_bytes); ) {
			shim_off64_t sz = (mwc32() & 0x1fc00) + KB;

			ret = shim_sync_file_range(fd, offset, sz, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail_err("sync_file_range (forward)");
				break;
			}
			offset += sz;
		}
		if (!g_keep_stressing_flag)
			break;

		if (stress_sync_allocate(args, fd, sync_file_bytes) < 0)
			break;
		for (offset = 0; g_keep_stressing_flag &&
		     (offset < (shim_off64_t)sync_file_bytes); ) {
			shim_off64_t sz = (mwc32() & 0x1fc00) + KB;

			ret = shim_sync_file_range(fd, sync_file_bytes - offset, sz, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail_err("sync_file_range (reverse)");
				break;
			}
			offset += sz;
		}
		if (!g_keep_stressing_flag)
			break;

		if (stress_sync_allocate(args, fd, sync_file_bytes) < 0)
			break;
		for (i = 0; i < g_keep_stressing_flag &&
		     ((shim_off64_t)(sync_file_bytes / (128 * KB))); i++) {
			offset = (mwc64() % sync_file_bytes) & ~((128 * KB) - 1);
			ret = shim_sync_file_range(fd, offset, 128 * KB, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail_err("sync_file_range (random)");
				break;
			}
		}
		inc_counter(args);
	} while (keep_stressing());

err:
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sync_file_info = {
	.stressor = stress_sync_file,
	.class = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_sync_file_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
