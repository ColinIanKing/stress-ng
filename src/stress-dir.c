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

	dir_dirs = stress_get_uint64(opt);
	stress_check_range("dir-dirs", dir_dirs,
		MIN_DIR_DIRS, MAX_DIR_DIRS);
	return stress_set_setting("dir-dirs", TYPE_ID_UINT64, &dir_dirs);
}

#if defined(__DragonFly__)
#define d_reclen d_namlen
#endif

/*
 *  stress_dir_sync()
 *	attempt to sync a directory
 */
static inline void stress_dir_sync(const int dirfd)
{
#if defined(O_DIRECTORY)
	/*
	 *  The interesting part of fsync is that in
	 *  theory we can fsync a read only file and
	 *  this could be a directory too. So try and
	 *  sync.
	 */
	(void)shim_fsync(dirfd);
#else
	(void)dirfd;
#endif
}

/*
 *  stress_dir_flock()
 *	naive exercising of a flock on a directory fd
 */
static inline void stress_dir_flock(const int dirfd)
{
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN) &&		\
    defined(O_DIRECTORY)
	if (dirfd >= 0) {
		if (flock(dirfd, LOCK_EX) == 0)
			(void)flock(dirfd, LOCK_UN);
	}
#else
	(void)dirfd;
#endif
}

/*
 *  stress_dir_truncate()
 *	exercise illegal truncate call on directory fd
 */
static inline void stress_dir_truncate(const char *path, const int dirfd)
{
	int ret;

	if (dirfd >= 0) {
		/* Invalid ftruncate */
		ret = ftruncate(dirfd, 0);
		(void)ret;
	}

	/* Invalid truncate */
	ret = truncate(path, 0);
	(void)ret;
}

/*
 *  stress_dir_mmap()
 *	attempt to mmap a directory
 */
static inline void stress_dir_mmap(const int dirfd, const size_t page_size)
{
#if defined(O_DIRECTORY)
	if (dirfd >= 0) {
		void *ptr;

		ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, dirfd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, page_size);
	}
#else
	(void)dirfd;
	(void)page_size;
#endif
}

/*
 *  stress_dir_read()
 *	read and stat all dentries
 */
static void stress_dir_read(
	const stress_args_t *args,
	const char *path)
{
	DIR *dp;
	struct dirent *de;

	dp = opendir(path);
	if (!dp)
		return;

	while (keep_stressing(args) && ((de = readdir(dp)) != NULL)) {
		char filename[PATH_MAX];
		struct stat statbuf;

		if (de->d_reclen == 0) {
			pr_fail("%s: read a zero sized directory entry\n", args->name);
			break;
		}
		stress_mk_filename(filename, sizeof(filename), path, de->d_name);
		(void)stat(filename, &statbuf);
	}

	(void)closedir(dp);
}

/*
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(
	const stress_args_t *args,
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
 *  stress_mkdir()
 *	exercise mdir/mkdirat calls
 */
static int stress_mkdir(const int dirfd, const char *path, const int mode)
{
	int ret;

#if defined(HAVE_MKDIRAT)
	/*
	 *  50% of the time use mkdirat rather than mkdir
	 */
	if ((dirfd >= 0) && stress_mwc1()) {
		char tmp[PATH_MAX], *filename;

		(void)shim_strlcpy(tmp, path, sizeof(tmp));
		filename = basename(tmp);

		ret = mkdirat(dirfd, filename, mode);
	} else {
		ret = mkdir(path, mode);
	}
#else
	ret = mkdir(path, mode);
#endif
	(void)dirfd;

	return ret;
}

/*
 *  stress_invalid_mkdir()
 *	exercise invalid mkdir path
 */
static void stress_invalid_mkdir(const char *path)
{
	int ret;
	char filename[PATH_MAX + 16];
	size_t len;

	(void)shim_strlcpy(filename, path, sizeof(filename));
	(void)shim_strlcat(filename, "/", sizeof(filename));
	len = strlen(filename);
	(void)stress_strnrnd(filename + len, sizeof(filename) - len);
	ret = mkdir(filename,  S_IRUSR | S_IWUSR);
	if (ret == 0)
		(void)rmdir(filename);
}

/*
 *  stress_invalid_rmdir()
 *	exercise invalid rmdir paths
 */
static void stress_invalid_rmdir(const char *path)
{
	int ret;
	char filename[PATH_MAX + 16];

	(void)shim_strlcpy(filename, path, sizeof(filename));
	/* remove . - exercise EINVAL error */
	(void)shim_strlcat(filename, "/.", sizeof(filename));
	ret = rmdir(filename);
	(void)ret;

	/* remove /.. - exercise ENOTEMPTY error */
	(void)shim_strlcat(filename, ".", sizeof(filename));
	ret = rmdir(filename);
	(void)ret;

	/* remove / - exercise EBUSY error */
	ret = rmdir("/");
	(void)ret;
}

/*
 *  stress_dir
 *	stress directory mkdir and rmdir
 */
static int stress_dir(const stress_args_t *args)
{
	int ret;
	uint64_t dir_dirs = DEFAULT_DIR_DIRS;
	char pathname[PATH_MAX];
	int dirfd = -1;

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);
	(void)stress_get_setting("dir-dirs", &dir_dirs);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

#if defined(O_DIRECTORY)
	dirfd = open(pathname, O_DIRECTORY | O_RDONLY);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint64_t i, n = dir_dirs;

		stress_dir_mmap(dirfd, args->page_size);
		stress_dir_flock(dirfd);
		stress_dir_truncate(pathname, dirfd);

		for (i = 0; keep_stressing(args) && (i < n); i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;

			(void)stress_temp_filename_args(args,
				path, sizeof(path), gray_code);
			if (stress_mkdir(dirfd, path, S_IRUSR | S_IWUSR) < 0) {
				if ((errno != ENOSPC) &&
				    (errno != ENOMEM) &&
				    (errno != EMLINK)) {
					pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
						args->name, path, errno, strerror(errno));
					break;
				}
			}
			inc_counter(args);
		}
		stress_invalid_mkdir(pathname);
		stress_invalid_rmdir(pathname);

		if (!keep_stressing(args)) {
			stress_dir_tidy(args, i);
			break;
		}
		stress_dir_read(args, pathname);
		stress_dir_tidy(args, i);

		if (!keep_stressing(args))
			break;
		stress_dir_sync(dirfd);
		(void)sync();

		inc_counter(args);
	} while (keep_stressing(args));

	/* exercise invalid path */
	{
		int rmret;

		rmret = rmdir("");
		(void)rmret;
	}

#if defined(O_DIRECTORY)
	if (dirfd >= 0)
		(void)close(dirfd);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_temp_dir_rm_args(args);

	return ret;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_dir_dirs,	stress_set_dir_dirs },
	{ 0,		NULL }
};

stressor_info_t stress_dir_info = {
	.stressor = stress_dir,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
