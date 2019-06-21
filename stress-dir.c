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
	{ NULL,	"dir N",	"start N directory thrashing stressors" },
	{ NULL,	"dir-ops N",	"stop after N directory bogo operations" },
	{ NULL,	"dir-dirs N",	"select number of directories to exercise dir on" },
	{ NULL,	NULL,		NULL }
};

/*
 *  stress_set_dir_dirs()
 *      set number of dir directories from given option string
 */
static int stress_set_dir_dirs(const char *opt)
{
	uint64_t dir_dirs;

	dir_dirs = get_uint64(opt);
	check_range("dir-dirs", dir_dirs,
		MIN_DIR_DIRS, MAX_DIR_DIRS);
	return set_setting("dir-dirs", TYPE_ID_UINT64, &dir_dirs);
}

#if defined(__DragonFly__)
#define d_reclen d_namlen
#endif

/*
 *  stress_dir_sync()
 *	attempt to sync a directory
 */
static void stress_dir_sync(const char *path)
{
#if defined(O_DIRECTORY)
	int fd;

	fd = open(path, O_RDONLY | O_DIRECTORY);
	if (fd < 0)
		return;

	/*
	 *  The interesting part of fsync is that in
	 *  theory we can fsync a read only file and
	 *  this could be a directory too. So try and
	 *  sync.
	 */
	(void)shim_fsync(fd);
	(void)close(fd);
#else
	(void)path;
#endif
}

/*
 *  stress_dir_read()
 *	read all dentries
 */
static void stress_dir_read(
	const args_t *args,
	const char *path)
{
	DIR *dp;
	struct dirent *de;

	dp = opendir(path);
	if (!dp)
		return;

	while ((de = readdir(dp)) != NULL) {
		if (de->d_reclen == 0) {
			pr_fail("%s: read a zero sized directory entry\n", args->name);
			break;
		}
	}

	(void)closedir(dp);
}

/*
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(
	const args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename_args(args,
			path, sizeof(path), gray_code);
		(void)rmdir(path);
	}
}

/*
 *  stress_dir
 *	stress directory mkdir and rmdir
 */
static int stress_dir(const args_t *args)
{
	int ret;
	uint64_t dir_dirs = DEFAULT_DIR_DIRS;
	char pathname[PATH_MAX];

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);

	(void)get_setting("dir-dirs", &dir_dirs);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	do {
		uint64_t i, n = dir_dirs;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;

			(void)stress_temp_filename_args(args,
				path, sizeof(path), gray_code);
			if (mkdir(path, S_IRUSR | S_IWUSR) < 0) {
				if ((errno != ENOSPC) && (errno != ENOMEM)) {
					pr_fail_err("mkdir");
					n = i;
					break;
				}
			}

			if (!keep_stressing())
				goto abort;

			inc_counter(args);
		}
		stress_dir_sync(pathname);
		stress_dir_read(args, pathname);
		stress_dir_tidy(args, n);
		stress_dir_sync(pathname);
		if (!g_keep_stressing_flag)
			break;
		(void)sync();
	} while (keep_stressing());

abort:
	/* force unlink of all files */
	pr_tidy("%s: removing %" PRIu64 " directories\n",
		args->name, dir_dirs);
	stress_dir_tidy(args, dir_dirs);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_dir_dirs,	stress_set_dir_dirs },
	{ 0,		NULL }
};

stressor_info_t stress_dir_info = {
	.stressor = stress_dir,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
