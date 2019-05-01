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
	{ NULL,	"dirdeep N",		"start N directory depth stressors" },
	{ NULL,	"dirdeep-ops N",	"stop after N directory depth bogo operations" },
	{ NULL,	"dirdeep-dirs N",	"create N directories per level" },
	{ NULL,	"dirdeep-inodes N",	"create a maximum N inodes (N can also be %)" },
	{ NULL,	NULL,			NULL }
};

/*
 *  stress_set_dirdeep_dirs()
 *      set number of dirdeep directories from given option string
 */
static int stress_set_dirdeep_dirs(const char *opt)
{
	uint32_t dirdeep_dirs;

	dirdeep_dirs = get_uint32(opt);

	check_range("dirdeep-dirs", dirdeep_dirs, 1, 10);
	return set_setting("dirdeep-dirs", TYPE_ID_UINT32, &dirdeep_dirs);
}

/*
 *  stress_set_dirdeep_inodes()
 *      set max number of inodes to consume
 */
static int stress_set_dirdeep_inodes(const char *opt)
{
	uint64_t inodes = stress_get_filesystem_available_inodes();
	uint64_t dirdeep_inodes;

	dirdeep_inodes = get_uint64_percent(opt, 1, inodes,
		"Cannot determine number of available free inodes");
	return set_setting("dirdeep-inodes", TYPE_ID_UINT64, &dirdeep_inodes);
}

/*
 *  stress_dirdeep_sync()
 *	attempt to sync a directory
 */
static void stress_dirdeep_sync(const char *path)
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
 *  stress_dirdeep_make()
 *	depth-first tree creation, create lots of sub-trees with
 *	dirdeep_dir number of subtress per level.
 */
static void stress_dirdeep_make(
	const args_t *args,
	const char *linkpath,
	char *const path,
	const size_t len,
	const size_t path_len,
	const uint32_t dirdeep_dirs,
	const uint64_t dirdeep_inodes,
	const uint64_t inodes_target_free,
	uint64_t *min_inodes_free,
	uint32_t depth)
{
	uint32_t i;
	int ret;
	const uint64_t inodes_avail = stress_get_filesystem_available_inodes();

	if (*min_inodes_free > inodes_avail)
		*min_inodes_free = inodes_avail;

	if (inodes_avail <= inodes_target_free)
		return;
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
	if (!depth) {
		int fd;

		fd = creat(linkpath, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd < 0) {
			pr_fail_err("create");
			return;
		}
		(void)close(fd);
	}

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
		stress_dirdeep_make(args, linkpath, path, len + 2, path_len,
				dirdeep_dirs, dirdeep_inodes, inodes_target_free,
				min_inodes_free, depth + 1);
	}
	path[len] = '\0';

	stress_dirdeep_sync(path);
}

/*
 *  stress_dir_exercise()
 *	exercise files and directories in the tree
 */
static void stress_dir_exercise(
	const args_t *args,
	char *const path,
	const size_t len,
	const size_t path_len)
{
	struct dirent **namelist;
	int i, n;
#if defined(HAVE_FUTIMENS)
	const double now = time_now();
	const time_t sec = (time_t)now;
	const long nsec = (long)((now - (double)sec) * 1000000000.0);
	struct timespec timespec[2] = {
		{ sec, nsec },
		{ sec, nsec }
	};
#endif

	if (len + 2 >= path_len)
		return;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0)
		return;

	for (i = 0; (i < n) && keep_stressing(); i++) {
		if (namelist[i]->d_name[0] == '.')
			continue;

		path[len] = '/';
		path[len + 1] = namelist[i]->d_name[0];
		path[len + 2] = '\0';

		if (isdigit((int)namelist[i]->d_name[0])) {
			stress_dir_exercise(args, path, len + 2, path_len);
		} else {
			int fd;

			/* This will update atime only */
			fd = open(path, O_RDONLY);
			if (fd >= 0) {
				const uint16_t rnd = mwc16();
#if defined(HAVE_FUTIMENS)
				int ret = futimens(fd, timespec);
				(void)ret;
#endif
				/* Occasional flushing */
				if (rnd >= 0xfff0) {
#if defined(HAVE_SYNCFS)
					(void)syncfs(fd);
#else
					(void)sync();
#endif
				} else if (rnd > 0xff40) {
					(void)shim_fsync(fd);
				}
				(void)close(fd);
			}
			inc_counter(args);
		}
	}
	path[len] = '\0';
	if (namelist) {
		for (i = 0; i < n; i++)
			free(namelist[i]);
		free(namelist);
	}
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

		if (isdigit((int)namelist[n]->d_name[0]))
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
static int stress_dirdeep(const args_t *args)
{
	int ret = EXIT_SUCCESS;
	char path[PATH_MAX + 16];
	char linkpath[sizeof(path)];
	char rootpath[PATH_MAX];
	size_t path_len;
	uint32_t dirdeep_dirs = 1;
	uint64_t dirdeep_inodes = ~0ULL;
	const uint64_t inodes_avail = stress_get_filesystem_available_inodes();
	uint64_t inodes_target_free;
	uint64_t min_inodes_free = ~0ULL;

        (void)get_setting("dirdeep-dirs", &dirdeep_dirs);
        (void)get_setting("dirdeep-inodes", &dirdeep_inodes);

	inodes_target_free = (inodes_avail > dirdeep_inodes) ?
		inodes_avail - dirdeep_inodes : 0;

	(void)stress_temp_dir_args(args, rootpath, sizeof(rootpath));
	path_len = strlen(rootpath);

	(void)shim_strlcpy(linkpath, rootpath, sizeof(linkpath));
	(void)shim_strlcat(linkpath, "/f", sizeof(linkpath) - 3);

	pr_inf("%s: %" PRIu64 " inodes available, exercising up to %" PRIu64 " inodes\n",
		args->name, inodes_avail, inodes_avail - inodes_target_free);

	(void)shim_strlcpy(path, rootpath, sizeof(path));
	stress_dirdeep_make(args, linkpath, path, path_len, sizeof(path),
		dirdeep_dirs, dirdeep_inodes, inodes_target_free, &min_inodes_free, 0);
	do {
		(void)shim_strlcpy(path, rootpath, sizeof(path));
		stress_dir_exercise(args, path, path_len, sizeof(path));
	} while (keep_stressing());

	(void)shim_strlcpy(path, rootpath, sizeof(path));
	pr_tidy("%s: removing directories\n", args->name);
	stress_dir_tidy(args, path, path_len, sizeof(path));

	pr_inf("%s: %" PRIu64 " inodes exercised\n", args->name, inodes_avail - min_inodes_free);
	if ((args->instance == 0) && (inodes_target_free < min_inodes_free))
		pr_inf("%s: note: specifying a larger --dirdeep setting or "
			"running the stressor for longer will use more "
			"inodes\n", args->name);

	return ret;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_dirdeep_dirs,	stress_set_dirdeep_dirs },
	{ OPT_dirdeep_inodes,	stress_set_dirdeep_inodes },
	{ 0,			NULL }
};

stressor_info_t stress_dirdeep_info = {
	.stressor = stress_dirdeep,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
