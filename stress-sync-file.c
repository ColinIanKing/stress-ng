// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"

#define MIN_SYNC_FILE_BYTES	(1 * MB)
#define MAX_SYNC_FILE_BYTES	(MAX_FILE_LIMIT)
#define DEFAULT_SYNC_FILE_BYTES	(1 * GB)

static const stress_help_t help[] = {
	{ NULL,	"sync-file N",	     "start N workers exercise sync_file_range" },
	{ NULL,	"sync-file-bytes N", "size of file to be sync'd" },
	{ NULL,	"sync-file-ops N",   "stop after N sync_file_range bogo operations" },
	{ NULL,	NULL,		     NULL }
};

#if defined(HAVE_SYNC_FILE_RANGE)
static const unsigned int sync_modes[] = {
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
	SYNC_FILE_RANGE_WRITE,
	SYNC_FILE_RANGE_WAIT_BEFORE,
	SYNC_FILE_RANGE_WAIT_AFTER,
	0	/* No-op */
};
#else
UNEXPECTED
#endif

static int stress_set_sync_file_bytes(const char *opt)
{
	off_t sync_file_bytes;

	sync_file_bytes = (off_t)stress_get_uint64_byte_filesystem(opt, 1);
	stress_check_range_bytes("sync_file-bytes", (uint64_t)sync_file_bytes,
		MIN_SYNC_FILE_BYTES, MAX_SYNC_FILE_BYTES);
	return stress_set_setting("sync_file-bytes", TYPE_ID_OFF_T, &sync_file_bytes);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_sync_file_bytes,	stress_set_sync_file_bytes },
	{ 0,			NULL }
};

#if defined(HAVE_SYNC_FILE_RANGE)

/*
 *  shrink and re-allocate the file to be sync'd
 *
 */
static int stress_sync_allocate(
	const stress_args_t *args,
	const int fd,
	const char *fs_type,
	const off_t sync_file_bytes)
{
	int ret;

	ret = ftruncate(fd, 0);
	if (ret < 0) {
		pr_err("%s: ftruncate failed: errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		return -errno;
	}

#if defined(HAVE_FDATASYNC)
	ret = shim_fdatasync(fd);
	if (ret < 0) {
		if ((errno == ENOSPC) || (errno == EINTR))
			return -errno;
		pr_fail("%s: fdatasync failed: errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		return -errno;
	}
#else
	UNEXPECTED
#endif

	ret = shim_fallocate(fd, 0, (off_t)0, sync_file_bytes);
	if (ret < 0) {
		if (errno == EINTR)
			return 0;
		if (errno == ENOSPC)
			return -errno;
		pr_err("%s: fallocate failed: errno=%d (%s)%s\n",
			args->name, errno, strerror(errno), fs_type);
		return -errno;
	}
	return 0;
}

/*
 *  stress_sync_file
 *	stress the sync_file_range system call
 */
static int stress_sync_file(const stress_args_t *args)
{
	int fd, ret;
	const int bad_fd = stress_get_bad_fd();
	off_t sync_file_bytes = DEFAULT_SYNC_FILE_BYTES;
	char filename[PATH_MAX];
	const char *fs_type;

	if (!stress_get_setting("sync_file-bytes", &sync_file_bytes)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			sync_file_bytes = MAXIMIZED_FILE_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			sync_file_bytes = MIN_SYNC_FILE_BYTES;
	}
	sync_file_bytes /= args->num_instances;
	if (sync_file_bytes < (off_t)MIN_SYNC_FILE_BYTES)
		sync_file_bytes = (off_t)MIN_SYNC_FILE_BYTES;

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC)) {
			pr_inf_skip("%s: cannot create file to sync on, skipping stressor: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_NO_RESOURCE;
		}

		ret = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}
	stress_file_rw_hint_short(fd);

	fs_type = stress_get_fs_type(filename);
	(void)shim_unlink(filename);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		shim_off64_t i, offset;
		const size_t mode_index = stress_mwc32modn(SIZEOF_ARRAY(sync_modes));
		const unsigned int mode = sync_modes[mode_index];

		ret = stress_sync_allocate(args, fd, fs_type, sync_file_bytes);
		if (ret < 0) {
			if (ret == -ENOSPC)
				continue;
			break;
		}
		for (offset = 0; stress_continue_flag() &&
		     (offset < (shim_off64_t)sync_file_bytes); ) {
			const shim_off64_t sz = (stress_mwc32() & 0x1fc00) + KB;

			ret = shim_sync_file_range(fd, offset, sz, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf_skip("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail("%s: sync_file_range (forward), errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				break;
			}
			offset += sz;
		}
		if (!stress_continue_flag())
			break;

		/*
		 *  Exercise sync_file_range with a bad fd
		 */
		VOID_RET(int, shim_sync_file_range(bad_fd, 0, 4096, mode));

		/*
		 *  Exercise sync_file_range with illegal offset and nbytes
		 */
		VOID_RET(int, shim_sync_file_range(fd, -1, 4096, mode));
		VOID_RET(int, shim_sync_file_range(fd, 0, -1, mode));

		/* Sync from halfway along file to end */
		VOID_RET(int, shim_sync_file_range(fd, sync_file_bytes << 2, 0, mode));

		ret = stress_sync_allocate(args, fd, fs_type, sync_file_bytes);
		if (ret < 0) {
			if (ret == -ENOSPC)
				continue;
			break;
		}
		for (offset = 0; stress_continue_flag() &&
		     (offset < (shim_off64_t)sync_file_bytes); ) {
			const shim_off64_t sz = (stress_mwc32() & 0x1fc00) + KB;

			ret = shim_sync_file_range(fd, sync_file_bytes - offset, sz, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf_skip("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail("%s: sync_file_range (reverse), errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				break;
			}
			offset += sz;
		}
		if (!stress_continue_flag())
			break;

		ret = stress_sync_allocate(args, fd, fs_type, sync_file_bytes);
		if (ret < 0) {
			if (ret == -ENOSPC)
				continue;
			break;
		}
		for (i = 0; stress_continue_flag() &&
		     (i < (shim_off64_t)sync_file_bytes / (shim_off64_t)(128 * KB)); i++) {
			offset = (shim_off64_t)(stress_mwc64modn((uint64_t)sync_file_bytes) & ~((128 * KB) - 1));

			ret = shim_sync_file_range(fd, offset, 128 * KB, mode);
			if (ret < 0) {
				if (errno == ENOSYS) {
					pr_inf_skip("%s: skipping stressor, sync_file_range is not implemented\n",
						args->name);
					goto err;
				}
				pr_fail("%s: sync_file_range (random), errno=%d (%s)%s\n",
					args->name, errno, strerror(errno), fs_type);
				break;
			}
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)close(fd);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sync_file_info = {
	.stressor = stress_sync_file,
	.class = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
stressor_info_t stress_sync_file_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_IO | CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sync_file_range() system call"
};
#endif
