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
#include "core-killpid.h"

#include <ctype.h>

#if defined(HAVE_SYS_FILE_H)
#include <sys/file.h>
#endif

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

#define MIN_DIR_DIRS		(64)
#define MAX_DIR_DIRS		(65536)
#define DEFAULT_DIR_DIRS	(8192)

static const stress_help_t help[] = {
	{ NULL,	"dir N",	"start N directory thrashing stressors" },
	{ NULL,	"dir-dirs N",	"select number of directories to exercise dir on" },
	{ NULL,	"dir-ops N",	"stop after N directory bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__DragonFly__)
#define d_reclen d_namlen
#endif

#if defined(HAVE_MODE_T)
typedef mode_t	shim_mode_t;
#else
typedef int	shim_mode_t;
#endif

/*
 *  stress_dir_sync()
 *	attempt to sync a directory
 */
static inline void stress_dir_sync(const int dir_fd)
{
#if defined(O_DIRECTORY)
	/*
	 *  The interesting part of fsync is that in
	 *  theory we can fsync a read only file and
	 *  this could be a directory too. So try and
	 *  sync.
	 */
	(void)shim_fsync(dir_fd);
#else
	(void)dir_fd;
#endif
}

/*
 *  stress_dir_flock()
 *	naive exercising of a flock on a directory fd
 */
static inline void stress_dir_flock(const int dir_fd)
{
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN) &&		\
    defined(O_DIRECTORY)
	if (dir_fd >= 0) {
		if (flock(dir_fd, LOCK_EX) == 0)
			(void)flock(dir_fd, LOCK_UN);
	}
#else
	(void)dir_fd;
#endif
}

/*
 *  stress_dir_truncate()
 *	exercise illegal truncate call on directory fd
 */
static inline void stress_dir_truncate(const char *path, const int dir_fd)
{
	if (dir_fd >= 0) {
		/* Invalid ftruncate */
		VOID_RET(int, ftruncate(dir_fd, 0));
	}

	/* Invalid truncate */
	VOID_RET(int, truncate(path, 0));
}

/*
 *  stress_dir_mmap()
 *	attempt to mmap a directory
 */
static inline void stress_dir_mmap(const int dir_fd, const size_t page_size)
{
#if defined(O_DIRECTORY)
	if (dir_fd >= 0) {
		void *ptr;

		ptr = mmap(NULL, page_size, PROT_READ, MAP_PRIVATE | MAP_ANONYMOUS, dir_fd, 0);
		if (ptr != MAP_FAILED)
			(void)munmap(ptr, page_size);
	}
#else
	(void)dir_fd;
	(void)page_size;
#endif
}

/*
 *  stress_dir_read()
 *	read and stat all dentries
 */
static int stress_dir_read(
	stress_args_t *args,
	const char *path)
{
	DIR *dp;
	const struct dirent *de;

	dp = opendir(path);
	if (!dp)
		return -1;

	while (LIKELY(stress_continue(args) && ((de = readdir(dp)) != NULL))) {
		char filename[PATH_MAX];
		struct stat statbuf;
		int fd;

#if !defined(__CYGWIN__)
		if (de->d_reclen == 0) {
			pr_fail("%s: read a zero sized directory entry\n", args->name);
			break;
		}
#endif
		stress_mk_filename(filename, sizeof(filename), path, de->d_name);
		fd = open(filename, O_RDONLY);
		if (fd >= 0) {
			(void)shim_fstat(fd, &statbuf);
			(void)close(fd);
		} else {
			(void)shim_stat(filename, &statbuf);
		}
	}

	(void)closedir(dp);

	return 0;
}

/*
 *  stress_dir_rename()
 *	rename all directories. Try to reproduce fixed in linux
 * 	commit 9b378f6ad48cfa195ed868db9123c09ee7ec5ea2
 *	("btrfs: fix infinite directory reads")
 */
static int stress_dir_rename(
	stress_args_t *args,
	const char *path)
{
	DIR *dp;
	const struct dirent *de;
	char new_filename[PATH_MAX];
	char tmp[32];

	(void)snprintf(tmp, sizeof(tmp), "rename-%" PRIu32 "-%" PRIdMAX, stress_mwc32(), (intmax_t)getpid());
	stress_mk_filename(new_filename, sizeof(new_filename), path, tmp);

	dp = opendir(path);
	if (!dp)
		return -1;

	while (LIKELY(stress_continue(args) && ((de = readdir(dp)) != NULL))) {
		char old_filename[PATH_MAX];

#if !defined(__CYGWIN__)
		if (de->d_reclen == 0) {
			pr_fail("%s: read a zero sized directory entry\n", args->name);
			break;
		}
#endif
		if (de->d_name[0] == '.')
			continue;

		stress_mk_filename(old_filename, sizeof(old_filename), path, de->d_name);
		if (rename(old_filename, new_filename) < 0) {
			pr_fail("%s: rename %s to %s failed, errno=%d (%s)\n",
				args->name, old_filename, new_filename,
				errno, strerror(errno));
			break;
		}
		if (rename(new_filename, old_filename) < 0) {
			pr_fail("%s: rename %s to %s failed, errno=%d (%s)\n",
				args->name, new_filename, old_filename,
				errno, strerror(errno));
			break;
		}
	}
	(void)closedir(dp);

	return 0;
}

/*
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(
	stress_args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		const uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename_args(args,
			path, sizeof(path), gray_code);
		(void)shim_rmdir(path);
	}
}

/*
 *  stress_mkdir()
 *	exercise mdir/mkdirat calls
 */
static int stress_mkdir(const int dir_fd, const char *path, const int mode)
{
	int ret;

#if defined(HAVE_MKDIRAT)
	/*
	 *  50% of the time use mkdirat rather than mkdir
	 */
	if ((dir_fd >= 0) && stress_mwc1()) {
		char tmp[PATH_MAX];
		const char *filename;

		(void)shim_strscpy(tmp, path, sizeof(tmp));
		filename = basename(tmp);

		ret = mkdirat(dir_fd, filename, (shim_mode_t)mode);
	} else {
		ret = mkdir(path, (shim_mode_t)mode);
	}
#else
	ret = mkdir(path, mode);
#endif
	(void)dir_fd;

	return ret;
}

/*
 *  stress_invalid_mkdir()
 *	exercise invalid mkdir path
 */
static inline void stress_invalid_mkdir(const char *path)
{
	int ret;
	char filename[PATH_MAX + 16];
	size_t len;

	(void)shim_strscpy(filename, path, sizeof(filename));
	(void)shim_strlcat(filename, "/", sizeof(filename));
	len = strlen(filename);
	(void)stress_rndstr(filename + len, sizeof(filename) - len);
	ret = mkdir(filename,  S_IRUSR | S_IWUSR);
	if (ret == 0)
		(void)shim_rmdir(filename);
}

/*
 *  stress_invalid_mkdirat()
 *	exercise invalid mkdirat fd
 */
static inline void stress_invalid_mkdirat(const int bad_fd)
{
#if defined(HAVE_MKDIRAT)
	VOID_RET(int, mkdirat(bad_fd, "bad", S_IRUSR | S_IWUSR));
#else
	(void)bad_fd;
#endif
}

/*
 *  stress_invalid_rmdir()
 *	exercise invalid rmdir paths
 */
static inline void stress_invalid_rmdir(const char *path)
{
	char filename[PATH_MAX + 16];

	(void)shim_strscpy(filename, path, sizeof(filename));
	/* remove . - exercise EINVAL error */
	(void)shim_strlcat(filename, "/.", sizeof(filename));
	VOID_RET(int, shim_rmdir(filename));

	/* remove /.. - exercise ENOTEMPTY error */
	(void)shim_strlcat(filename, ".", sizeof(filename));
	VOID_RET(int, shim_rmdir(filename));

	/* remove / - exercise EBUSY error */
	VOID_RET(int, shim_rmdir("/"));
}

/*
 *  stress_dir_read_concurrent()
 *	read a directory concurrently with directory
 *	file activity to exercise kernel locking on
 *	the directory
 */
static inline void stress_dir_read_concurrent(
	stress_args_t *args,
	const char *pathname)
{
	(void)shim_nice(1);
	(void)shim_nice(1);
	(void)shim_nice(1);

	do {
		if (stress_dir_read(args, pathname) < 0)
			return;
	} while (stress_continue(args));
}

/*
 *  stress_dir_touch()
 *	create a small file
 */
static int stress_dir_touch(
	stress_args_t *args,
	const char *filename)
{
	int fd;

	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_fail("%s: cannot create file %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		return -1;
	}
	if (write(fd, "data", 4) < 0) {
		pr_inf("%s: failed to write to file %s, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
		(void)close(fd);
		return -1;
	}
	(void)close(fd);
	return 0;
}

/*
 *  stress_dir_readdir()
 *	exercise populating a new directory and checking to see if
 *	rewinddir and readdir pick up all the new entries.
 */
static int stress_dir_readdir(
	stress_args_t *args,
	const char *pathname)
{
	DIR *dir;
	char dirpath[PATH_MAX + 64];
	char filename[PATH_MAX + 70];
	int rc = 0, i, got_mask, all_mask;
	const struct dirent *de;

	(void)snprintf(dirpath, sizeof(dirpath), "%s/test-%" PRIdMAX "-%" PRIu32, pathname,
		(intmax_t)getpid(), stress_mwc32());
	if (mkdir(dirpath, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
		pr_fail("%s: cannot mkdir %s, errno=%d (%s)\n",
			args->name, dirpath, errno, strerror(errno));
		rc = -1;
		goto err_rmdir;
	}

	/*
	 *  Check if readdir + rewinddir will pick up new files
	 */
	dir = opendir(dirpath);
	if (!dir) {
		pr_fail("%s: cannot opendir %s, errno=%d (%s)\n",
			args->name, dirpath, errno, strerror(errno));
		rc = -1;
		goto err_rmdir;
	}
	all_mask = 0;
	for (i = 0; i < 10; i++) {
		all_mask |= (1U << i);
		(void)snprintf(filename, sizeof(filename), "%s/%d", dirpath, i);
		if (stress_dir_touch(args, filename) < 0) {
			(void)closedir(dir);
			rc = -1;
			goto err_rm_files;
		}
	}

	rewinddir(dir);
	got_mask = 0;
	while ((de = readdir(dir))) {
		if (isdigit((unsigned char)de->d_name[0])) {
			const int d = atoi(de->d_name);

			got_mask |= (1U << d);
		}
	}
	(void)closedir(dir);

	if (got_mask != all_mask) {
		pr_fail("%s: rewinddir and readdir did not find all the files in directory %s\n",
			args->name, dirpath);
		rc = -1;
	}

err_rm_files:
	for (i = 0; i < 10; i++) {
		(void)snprintf(filename, sizeof(filename), "%s/%d", dirpath, i);
		(void)unlink(filename);
	}
err_rmdir:
	(void)rmdir(dirpath);

	return rc;
}

/*
 *  stress_dir
 *	stress directory mkdir and rmdir
 */
static int stress_dir(stress_args_t *args)
{
	int ret;
	uint64_t dir_dirs = DEFAULT_DIR_DIRS;
	char pathname[PATH_MAX];
	int dir_fd = -1;
	const int bad_fd = stress_get_bad_fd();
	pid_t pid;

	stress_temp_dir(pathname, sizeof(pathname), args->name,
		args->pid, args->instance);
	if (!stress_get_setting("dir-dirs", &dir_dirs)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			dir_dirs = MAX_DIR_DIRS;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			dir_dirs = MIN_DIR_DIRS;
	}


	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

#if defined(O_DIRECTORY)
	dir_fd = open(pathname, O_DIRECTORY | O_RDONLY);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	pid = fork();
	if (pid == 0) {
		stress_set_proc_state(args->name, STRESS_STATE_RUN);
		stress_dir_read_concurrent(args, pathname);
		_exit(0);
	}

	do {
		uint64_t i, n = dir_dirs;

		stress_dir_mmap(dir_fd, args->page_size);
		stress_dir_flock(dir_fd);
		stress_dir_truncate(pathname, dir_fd);

		for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
			char path[PATH_MAX];
			const uint64_t gray_code = (i >> 1) ^ i;

			(void)stress_temp_filename_args(args,
				path, sizeof(path), gray_code);
			(void)shim_force_rmdir(path);
			if (stress_mkdir(dir_fd, path, S_IRUSR | S_IWUSR) < 0) {
				if ((errno != ENOSPC) &&
				    (errno != ENOMEM) &&
				    (errno != EMLINK)) {
					pr_fail("%s: mkdir %s failed, errno=%d (%s)\n",
						args->name, path, errno, strerror(errno));
					ret = EXIT_FAILURE;
					break;
				}
			}
			stress_bogo_inc(args);
		}
		stress_invalid_mkdir(pathname);
		stress_invalid_rmdir(pathname);
		stress_invalid_mkdirat(bad_fd);

		if (UNLIKELY(!stress_continue(args))) {
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			stress_dir_tidy(args, i);
			break;
		}
		stress_dir_read(args, pathname);
		if (UNLIKELY(!stress_continue(args))) {
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			stress_dir_tidy(args, i);
			break;
		}
		stress_dir_rename(args, pathname);
		if (UNLIKELY(!stress_continue(args))) {
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
			stress_dir_tidy(args, i);
			break;
		}
		if (stress_dir_readdir(args, pathname) < 0)
			ret = EXIT_FAILURE;
		stress_dir_tidy(args, i);
		stress_dir_sync(dir_fd);
		shim_sync();

		stress_bogo_inc(args);
	} while (stress_continue(args));

	/* exercise invalid path */
	{
		int rmret;

		rmret = shim_rmdir("");
		(void)rmret;
	}

#if defined(O_DIRECTORY)
	if (dir_fd >= 0)
		(void)close(dir_fd);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (pid >= 0)
		(void)stress_kill_pid_wait(pid, NULL);
	(void)stress_temp_dir_rm_args(args);

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_dir_dirs, "dir-dirs", TYPE_ID_UINT64, MIN_DIR_DIRS, MAX_DIR_DIRS, NULL },
	END_OPT,
};

const stressor_info_t stress_dir_info = {
	.stressor = stress_dir,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
