/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#define DEFAULT_DIRS		(8192)

static const stress_help_t help[] = {
	{ NULL,	"mknod N",	"start N workers that exercise mknod" },
	{ NULL,	"mknod-ops N",	"stop after N mknod bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__linux__) &&	\
    defined(HAVE_MKNOD)

typedef struct {
	const mode_t	mode;
	const char 	*mode_str;
} stress_mknod_modes_t;

static const stress_mknod_modes_t modes[] = {
#if defined(S_IFIFO)
	{ S_IFIFO,	"S_IFIFO" },	/* FIFO */
#endif
#if defined(S_IFREG)
	{ S_IFREG,	"S_IFREG" },	/* Regular file */
#endif
#if defined(S_IFSOCK)
	{ S_IFSOCK,	"S_IFSOCK" },	/* named socket */
#endif
#if defined(S_IFDIR)
	{ S_IFDIR,	"S_IFDIR" },	/* directory */
#endif
};

/*
 *  stress_mknod_tidy()
 *	remove all files
 */
static void stress_mknod_tidy(
	stress_args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		const uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename_args(args,
			path, sizeof(path), gray_code);
		(void)shim_unlink(path);
	}
}

/*
 *  stress_mknod_find_dev()
 *	find the first device that matches the mode flag so that
 *	the dev major/minor can be copied when creating a special
 *	device file. This is instead of randomly guessing a wrong
 *	device dev number.
 */
static int stress_mknod_find_dev(mode_t mode, dev_t *dev)
{
	DIR *dir;
	const struct dirent *d;
	int rc = -1;

	(void)shim_memset(dev, 0, sizeof(*dev));

	dir = opendir("/dev");
	if (!dir)
		return -1;

	while ((d = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		struct stat statbuf;

		(void)snprintf(path, sizeof(path), "/dev/%s", d->d_name);
		if (shim_stat(path, &statbuf) < 0)
			continue;

		/* A match, cope it */
		if ((statbuf.st_mode & S_IFMT) == mode) {
			(void)shim_memcpy(dev, &statbuf.st_dev, sizeof(*dev));
			rc = 0;
			break;
		}
	}

	(void)closedir(dir);
	return rc;
}

/*
 *  stress_mknod_check_errno()
 * 	silently ignore errno numbers that are resource related
 *	and not the fault of the underlying mknod for failing.
 */
static int stress_mknod_check_errno(
	stress_args_t *args,
	const char *mode_str,
	const char *path,
	const int err)
{
	switch (err) {
	case EDQUOT:
	case ENOMEM:
	case ENOSPC:
	case EPERM:
	case EROFS:
	case EINVAL:
		/* Don't care about these */
		return 0;
	default:
		/* An error occurred that is worth reporting */
		pr_fail("%s: mknod %s on %s failed, errno=%d (%s)\n",
			args->name, mode_str, path, errno, strerror(errno));
		break;
	}
	return -1;
}

static int stress_do_mknod(
	const int dir_fd,
	const int bad_fd,
	const char *path,
	const mode_t mode,
	const dev_t dev)
{
	int ret;

#if defined(HAVE_MKNODAT)
	/*
	 *  50% of the time use mknodat rather than mknod
	 */
	if ((dir_fd >= 0) && stress_mwc1()) {
		char tmp[PATH_MAX];
		const char *filename;

		(void)shim_strscpy(tmp, path, sizeof(tmp));
		filename = basename(tmp);

		(void)shim_force_unlink(path);
		ret = mknodat(bad_fd, filename, mode, dev);
		if (ret == 0)
			(void)shim_unlink(path);

		ret = mknodat(dir_fd, filename, mode, dev);
	} else {
		(void)shim_force_unlink(path);
		ret = mknod(path, mode, dev);
	}
#else
	(void)shim_force_unlink(path);
	ret = mknod(path, mode, dev);
#endif
	(void)dir_fd;
	(void)bad_fd;

	return ret;
}

/*
 *  stress_mknod_test_dev()
 *	test char or block mknod special nodes
 */
static void stress_mknod_test_dev(
	stress_args_t *args,
	const int dir_fd,
	const int bad_fd,
	const mode_t mode,
	const char *mode_str,
	dev_t dev)
{
	char path[PATH_MAX];
	int ret;

	(void)stress_temp_filename_args(args, path, sizeof(path), stress_mwc32());

	ret = stress_do_mknod(dir_fd, bad_fd, path, mode, dev);
	if (ret < 0)
		(void)stress_mknod_check_errno(args, mode_str, path, errno);

	(void)shim_unlink(path);
}

/*
 *  stress_mknod
 *	stress mknod creates
 */
static int stress_mknod(stress_args_t *args)
{
	const size_t num_nodes = SIZEOF_ARRAY(modes);
	int ret;
	dev_t chr_dev, blk_dev;
	int chr_dev_ret, blk_dev_ret;
	int dir_fd = -1;
	const int bad_fd = stress_get_bad_fd();
#if defined(HAVE_MKNODAT) &&	\
    defined(O_DIRECTORY)
	char pathname[PATH_MAX];
#endif

	if (num_nodes == 0) {
		pr_err("%s: aborting, no valid mknod modes.\n",
			args->name);
		return EXIT_FAILURE;
	}

	chr_dev_ret = stress_mknod_find_dev(S_IFCHR, &chr_dev);
	blk_dev_ret = stress_mknod_find_dev(S_IFBLK, &blk_dev);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

#if defined(HAVE_MKNODAT) &&	\
    defined(O_DIRECTORY)
	stress_temp_dir(pathname, sizeof(pathname), args->name,
		args->pid, args->instance);
	dir_fd = open(pathname, O_DIRECTORY | O_RDONLY);
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register uint64_t i;
		const uint64_t n = DEFAULT_DIRS;

		if (chr_dev_ret == 0)
			stress_mknod_test_dev(args, dir_fd, bad_fd, S_IFCHR, "S_IFCHR", chr_dev);
		if (blk_dev_ret == 0)
			stress_mknod_test_dev(args, dir_fd, bad_fd, S_IFBLK, "S_IFBLK", blk_dev);

		for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
			char path[PATH_MAX];
			register const uint64_t gray_code = (i >> 1) ^ i;
			register const size_t j = stress_mwc32modn(num_nodes);

			(void)stress_temp_filename_args(args,
				path, sizeof(path), gray_code);
			if (stress_do_mknod(dir_fd, bad_fd, path, modes[j].mode | S_IRUSR | S_IWUSR, 0) < 0) {
				if (stress_mknod_check_errno(args, modes[j].mode_str, path, errno) < 0)
					continue;	/* Try again */
				break;
			}
			stress_bogo_inc(args);
		}

		stress_mknod_tidy(args, i);
		if (UNLIKELY(!stress_continue_flag()))
			break;
		shim_sync();
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (dir_fd >= 0)
		(void)close(dir_fd);

	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_mknod_info = {
	.stressor = stress_mknod,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_mknod_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "only supported on Linux"
};
#endif
