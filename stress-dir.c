/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

/*
 *  stress_set_dir_dirs()
 *      set number of dir directories from given option string
 */
void stress_set_dir_dirs(const char *opt)
{
	uint64_t dir_dirs;

	dir_dirs = get_uint64(opt);
	check_range("dir-dirs", dir_dirs,
		MIN_DIR_DIRS, MAX_DIR_DIRS);
	set_setting("dir-dirs", TYPE_ID_UINT64, &dir_dirs);
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
int stress_dir(const args_t *args)
{
	int ret;
	uint64_t dir_dirs = DEFAULT_DIR_DIRS;

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
		stress_dir_tidy(args, n);
		if (!g_keep_stressing_flag)
			break;
		sync();
	} while (keep_stressing());

abort:
	/* force unlink of all files */
	pr_tidy("%s: removing %" PRIu64 " directories\n",
		args->name, dir_dirs);
	stress_dir_tidy(args, dir_dirs);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}
