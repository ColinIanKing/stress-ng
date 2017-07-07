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

#include <dirent.h>

/*
 *  stress_set_dirdeep_dirs()
 *      set number of dirdeep directories from given option string
 */
void stress_set_dirdeep_dirs(const char *opt)
{
	uint32_t dirdeep_dirs;

	dirdeep_dirs = get_uint32(opt);
	check_range("dirdeep-dirs", dirdeep_dirs,
		1, 10);
	set_setting("dirdeep-dirs", TYPE_ID_UINT32, &dirdeep_dirs);
}

/*
 *  stress_dir_make()
 *	depth-first tree creation, create lots of sub-trees with
 *	dirdeep_dir number of subtress per level.
 */
static void stress_dir_make(
	const args_t *args,
	const char *linkpath,
	char *const path,
	const size_t len,
	const size_t path_len,
	const uint32_t dirdeep_dirs,
	uint32_t *depth)
{
	uint32_t i;
	int ret;

	if (len + 2 >= path_len)
		return;
	if (!keep_stressing())
		return;

	errno = 0;
	if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
		if ((errno == ENOSPC) || (errno == ENOMEM) ||
		    (errno == ENAMETOOLONG) || (errno == EDQUOT) ||
		    (errno == EMLINK)) {
			return;
		}
		pr_fail_err("mkdir");
		return;
	}
	inc_counter(args);

	/*
	 *  Top level, create file to symlink and link to
	 */
	if (!(*depth)) {
		int fd;

		fd = creat(linkpath, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd < 0) {
			pr_fail_err("create");
			return;
		}
		(void)close(fd);
	}

	(*depth)++;

	path[len] = '/';
	path[len + 1] = 's';	/* symlink */
	path[len + 2] = '\0';
	ret = symlink(linkpath, path);
	(void)ret;
	path[len + 1] = 'h';	/* hardlink */
	ret = link(linkpath, path);
	(void)ret;

	for (i = 0; i < dirdeep_dirs; i++) {
		path[len + 1] = '0' + i;
		stress_dir_make(args, linkpath, path, len + 2, path_len, dirdeep_dirs, depth);
	}
	path[len] = '\0';
}

/*
 *  stress_dir_tidy()
 *	clean up all files and directories in the tree
 */
static void stress_dir_tidy(
	const args_t *args,
	char *const path,
	const size_t len,
	const size_t path_len)
{
	struct dirent **namelist;
	int n;

	if (len + 2 >= path_len)
		return;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0)
		return;

	while (n--) {
		if (namelist[n]->d_name[0] == '.')
			continue;

		path[len] = '/';
		path[len + 1] = namelist[n]->d_name[0];
		path[len + 2] = '\0';

		if (isdigit(namelist[n]->d_name[0]))
			stress_dir_tidy(args, path, len + 2, path_len);
		else
			(void)unlink(path);

		free(namelist[n]);
	}
	path[len] = '\0';
	free(namelist);

	(void)rmdir(path);
}


/*
 *  stress_dir
 *	stress deep recursive directory mkdir and rmdir
 */
int stress_dirdeep(const args_t *args)
{
	int ret = EXIT_SUCCESS;
	char path[PATH_MAX + 16];
	char linkpath[sizeof(path)];
	char rootpath[PATH_MAX];
	size_t path_len;
	uint32_t dirdeep_dirs = 1;

        (void)get_setting("dirdeep-dirs", &dirdeep_dirs);

	(void)stress_temp_dir_args(args, rootpath, sizeof(rootpath));
	path_len = strlen(rootpath);

	strncpy(linkpath, rootpath, sizeof(linkpath));
	strncat(linkpath, "/f", sizeof(linkpath) - 3);

	do {
		uint32_t depth = 0;

		strncpy(path, rootpath, sizeof(path));
		stress_dir_make(args, linkpath, path, path_len, sizeof(path), dirdeep_dirs, &depth);
		strncpy(path, rootpath, sizeof(path));
		if (!keep_stressing())
			 pr_tidy("%s: removing directories\n", args->name);
		stress_dir_tidy(args, path, path_len, sizeof(path));
		sync();
	} while (keep_stressing());

	return ret;
}
