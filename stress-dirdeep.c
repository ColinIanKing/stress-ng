/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"dirdeep N",		"start N directory depth stressors" },
	{ NULL,	"dirdeep-ops N",	"stop after N directory depth bogo operations" },
	{ NULL,	"dirdeep-dirs N",	"create N directories per level" },
	{ NULL,	"dirdeep-inodes N",	"create a maximum N inodes (N can also be %)" },
	{ NULL,	NULL,			NULL }
};

/* digits and uppercase for very short directory names */
static const char stress_dir_names[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/*
 *  stress_set_dirdeep_dirs()
 *      set number of dirdeep directories from given option string
 */
static int stress_set_dirdeep_dirs(const char *opt)
{
	uint32_t dirdeep_dirs;

	dirdeep_dirs = stress_get_uint32(opt);

	stress_check_range("dirdeep-dirs", dirdeep_dirs, 1, strlen(stress_dir_names));
	return stress_set_setting("dirdeep-dirs", TYPE_ID_UINT32, &dirdeep_dirs);
}

/*
 *  stress_set_dirdeep_inodes()
 *      set max number of inodes to consume
 */
static int stress_set_dirdeep_inodes(const char *opt)
{
	uint64_t inodes = stress_get_filesystem_available_inodes();
	uint64_t dirdeep_inodes;

	dirdeep_inodes = stress_get_uint64_percent(opt, 1, inodes,
		"Cannot determine number of available free inodes");
	return stress_set_setting("dirdeep-inodes", TYPE_ID_UINT64, &dirdeep_inodes);
}

/*
 *  stress_dirdeep_make()
 *	depth-first tree creation, create lots of sub-trees with
 *	dirdeep_dir number of subtress per level.
 */
static bool stress_dirdeep_make(
	const stress_args_t *args,
	const char *linkpath,
	char *const path,
	const size_t len,
	const size_t path_len,
	const uint32_t dirdeep_dirs,
	const uint64_t dirdeep_inodes,
	const uint64_t inodes_start,
	uint64_t *inodes_estimate,
	uint64_t *inodes_min,
	uint32_t depth)
{
	uint32_t i;
	int ret;
#if defined(HAVE_LINKAT) &&	\
    defined(O_DIRECTORY)
	int dir_fd;
#endif
	const uint64_t inodes_avail = stress_get_filesystem_available_inodes();

	if ((inodes_avail == 0) || (inodes_start == 0)) {
		if (*inodes_estimate > dirdeep_inodes)
			return true;
	} else {
		if (inodes_avail < *inodes_min)
			*inodes_min = inodes_avail;
		if (inodes_start - inodes_avail > dirdeep_inodes)
			return true;
	}
	if (len + 2 >= path_len)
		return true;
	if (!keep_stressing(args))
		return true;

	errno = 0;
	if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
		if ((errno == ENOSPC) || (errno == ENOMEM) ||
		    (errno == ENAMETOOLONG) || (errno == EDQUOT) ||
		    (errno == EMLINK) || (errno == EPERM)) {
			return true;
		}
		pr_fail("%s: mkdir failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return true;
	}
	inc_counter(args);
	(*inodes_estimate)++;

	/*
	 *  Top level, create file to symlink and link to
	 */
	if (!depth) {
		int fd;

		fd = creat(linkpath, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
		if (fd < 0) {
			pr_fail("%s: create %s failed, errno=%d (%s)\n",
				args->name, linkpath, errno, strerror(errno));
			return true;
		}
		(void)close(fd);
	}


	path[len] = '/';
	path[len + 1] = 's';	/* symlink */
	path[len + 2] = '\0';
	ret = symlink(linkpath, path);
	if (ret == 0)
		(*inodes_estimate)++;

	path[len + 1] = 'h';	/* hardlink */
	ret = link(linkpath, path);
	(void)ret;

	for (i = 0; keep_stressing(args) && (i < dirdeep_dirs); i++) {
		path[len + 1] = stress_dir_names[i];
		if (stress_dirdeep_make(args, linkpath, path, len + 2,
				path_len, dirdeep_dirs, dirdeep_inodes,
				inodes_start, inodes_estimate, inodes_min, depth + 1))
			break;
	}
	if (!keep_stressing(args))
		return true;
	path[len] = '\0';

#if defined(HAVE_LINKAT) &&	\
    defined(O_DIRECTORY)

	dir_fd = open(path, O_RDONLY | O_DIRECTORY);
	if (dir_fd >= 0) {
#if defined(AT_EMPTY_PATH) &&	\
    defined(O_PATH)
		int pathfd;
#endif

		/*
		 *  Exercise linkat onto hardlink h
		 */
		ret = linkat(dir_fd, "h", dir_fd, "a", 0);
		(void)ret;

		/*
		 *  Exercise linkat with invalid flags
		 */
		ret = linkat(dir_fd, "h", dir_fd, "i", ~0);
		if (ret == 0) {
			ret = shim_unlinkat(dir_fd, "i", 0);
			(void)ret;
		}
#if defined(AT_SYMLINK_FOLLOW)
		/*
		 *  Exercise linkat AT_SYMLINK_FOLLOW onto hardlink h
		 */
		ret = linkat(dir_fd, "h", dir_fd, "b", AT_SYMLINK_FOLLOW);
		(void)ret;
#endif
#if defined(AT_EMPTY_PATH) &&	\
    defined(O_PATH)
		/*
		 *  Exercise linkat AT_EMPTY_PATH onto hardlink h
		 */
		path[len] = '/';
		path[len + 1] = 'h';
		path[len + 2] = '\0';

		pathfd = open(path, O_PATH | O_RDONLY);
		if (pathfd >= 0) {
			/*
			 * Need CAP_DAC_READ_SEARCH for this to work,
			 * ignore return for now
			 */
			ret = linkat(pathfd, "", dir_fd, "c", AT_EMPTY_PATH);
			(void)ret;
			(void)close(pathfd);
		}
		path[len] = '\0';
#endif
#if defined(HAVE_UNLINKAT)
		ret = linkat(dir_fd, "h", dir_fd, "u", 0);
		if (ret == 0) {
			ret = shim_unlinkat(dir_fd, "u", 0);
			(void)ret;
		}
#endif
		/*
		 *  The interesting part of fsync is that in
		 *  theory we can fsync a read only file and
		 *  this could be a directory too. So try and
		 *  sync.
		 */
		(void)shim_fsync(dir_fd);
		(void)close(dir_fd);
	}
#endif
	return false;
}

/*
 *  stress_dir_exercise()
 *	exercise files and directories in the tree
 */
static int stress_dir_exercise(
	const stress_args_t *args,
	char *const path,
	const size_t len,
	const size_t path_len)
{
	struct dirent **namelist = NULL;
	int i, n;
#if defined(HAVE_FUTIMENS)
	const double now = stress_time_now();
	const time_t sec = (time_t)now;
	const long nsec = (long)((now - (double)sec) * (double)STRESS_NANOSECOND);
	struct timespec timespec[2] = {
		{ sec, nsec },
		{ sec, nsec }
	};
#endif
	if (!keep_stressing(args))
		return 0;

	if (len + 2 >= path_len)
		return 0;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0)
		return -1;

	for (i = 0; (i < n) && keep_stressing(args); i++) {
		register int ch;

		/* Sanity check */
		if (!namelist[n])
			continue;

		ch = (int)namelist[n]->d_name[0];
		if (ch == '.')
			continue;

		path[len] = '/';
		path[len + 1] = ch;
		path[len + 2] = '\0';

		if (isdigit(ch) || isupper(ch)) {
			stress_dir_exercise(args, path, len + 2, path_len);
		} else {
			int fd;

			/* This will update atime only */
			fd = open(path, O_RDONLY);
			if (fd >= 0) {
				const uint16_t rnd = stress_mwc16();
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
	stress_dirent_list_free(namelist, n);

	return 0;
}


/*
 *  stress_dir_tidy()
 *	clean up all files and directories in the tree
 */
static void stress_dir_tidy(
	const stress_args_t *args,
	char *const path,
	const size_t len,
	const size_t path_len)
{
	struct dirent **namelist = NULL;
	int n;

	if (len + 2 >= path_len)
		return;

	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0)
		return;

	while (n--) {
		register const int ch = (int)namelist[n]->d_name[0];

		if (ch == '.') {
			free(namelist[n]);
			continue;
		}

		path[len] = '/';
		path[len + 1] = ch;
		path[len + 2] = '\0';

		if (isdigit(ch) || isupper(ch)) {
			free(namelist[n]);
			stress_dir_tidy(args, path, len + 2, path_len);
		} else {
			free(namelist[n]);
			(void)shim_unlink(path);
		}
	}
	path[len] = '\0';
	free(namelist);

	(void)shim_rmdir(path);
}


/*
 *  stress_dir
 *	stress deep recursive directory mkdir and rmdir
 */
static int stress_dirdeep(const stress_args_t *args)
{
	int ret = EXIT_SUCCESS;
	char path[PATH_MAX + 16];
	char linkpath[sizeof(path)];
	char rootpath[PATH_MAX];
	size_t path_len;
	uint32_t dirdeep_dirs = 1;
	uint64_t dirdeep_inodes = ~0ULL;
	uint64_t inodes_start;
	uint64_t inodes_estimate;
	uint64_t inodes_min;
	uint64_t inodes_exercised;

	(void)stress_get_setting("dirdeep-dirs", &dirdeep_dirs);
	(void)stress_get_setting("dirdeep-inodes", &dirdeep_inodes);

	inodes_start = stress_get_filesystem_available_inodes();

	(void)stress_temp_dir_args(args, rootpath, sizeof(rootpath));
	path_len = strlen(rootpath);

	(void)stress_mk_filename(linkpath, sizeof(linkpath), rootpath, "/f");

	if (args->instance == 0) {
		if (inodes_start) {
			if (dirdeep_inodes > inodes_start)
				dirdeep_inodes = inodes_start;
			pr_dbg("%s: %" PRIu64 " inodes available, exercising up to %" PRIu64 " inodes\n",
				args->name, inodes_start, dirdeep_inodes);
		} else {
			pr_dbg("%s: unknown inodes available, exercising up to %" PRIu64 " inodes\n",
				args->name, dirdeep_inodes);
		}
	}

	(void)shim_strlcpy(path, rootpath, sizeof(path));
	inodes_estimate = 1;		/* created one for root */
	inodes_min = inodes_start;
	stress_dirdeep_make(args, linkpath, path, path_len, sizeof(path),
		dirdeep_dirs, dirdeep_inodes, inodes_start, &inodes_estimate, &inodes_min, 0);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)shim_strlcpy(path, rootpath, sizeof(path));
		if (stress_dir_exercise(args, path, path_len, sizeof(path)) < 0)
			break;
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)shim_strlcpy(path, rootpath, sizeof(path));
	pr_tidy("%s: removing directories\n", args->name);
	stress_dir_tidy(args, path, path_len, sizeof(path));

	inodes_exercised = (inodes_start == 0) ? inodes_estimate : inodes_start - inodes_min;
	pr_dbg("%s: %" PRIu64 " inodes exercised%s\n",
		args->name, inodes_exercised,
		(inodes_start == 0) ? " (estimated)" : "");

	if ((args->instance == 0) && (inodes_exercised < dirdeep_inodes))
		pr_inf("%s: note: specifying a larger --dirdeep or --dirdeep-dirs settings or "
			"running the stressor for longer will use more "
			"inodes\n", args->name);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_dirdeep_dirs,	stress_set_dirdeep_dirs },
	{ OPT_dirdeep_inodes,	stress_set_dirdeep_inodes },
	{ 0,			NULL }
};

stressor_info_t stress_dirdeep_info = {
	.stressor = stress_dirdeep,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
