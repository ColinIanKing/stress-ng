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
#include "core-mounts.h"

#define DEFAULT_LINKS	(8192)

#if defined(HAVE_LIBGEN_H)
#include <libgen.h>
#endif

static const stress_help_t hardlink_help[] = {
	{ NULL,	"link N",	"start N workers creating hard links" },
	{ NULL,	"link-ops N",	"stop after N link bogo operations" },
	{ NULL, "link-sync",	"enable sync'ing after linking/unlinking" },
	{ NULL,	NULL,		 NULL }
};

static const stress_help_t symlink_help[] = {
	{ NULL, "symlink N",	"start N workers creating symbolic links" },
	{ NULL, "symlink-ops N","stop after N symbolic link bogo operations" },
	{ NULL, "symlink-sync",	"enable sync'ing after symlinking/unsymlinking" },
	{ NULL, NULL,		NULL }
};

#define MOUNTS_MAX	(128)

static const stress_opt_t opts[] = {
	{ OPT_link_sync,    "link-sync",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_symlink_sync, "symlink-sync", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT
};

/*
 *  stress_link_unlink()
 *	remove all links
 */
static void stress_link_unlink(
	stress_args_t *args,
	const uint64_t n)
{
	register uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];

		(void)stress_temp_filename_args(args, path, sizeof(path), i);
		(void)shim_force_unlink(path);
		/*
		 *  Some file systems, such as minix 3 suffer from
		 *  contention when multiple stressors hammer unlink
		 *  so add a yield to try and help a little
		 */
		if ((i & 255) == 0)
			(void)shim_sched_yield();
	}
}

static inline size_t random_mount(const int mounts_max)
{
	return (size_t)stress_mwc32modn((uint32_t)mounts_max);
}

/*
 *  stress_link_generic
 *	stress links, generic case
 */
static int stress_link_generic(
	stress_args_t *args,
	int (*linkfunc)(const char *oldpath, const char *newpath),
	const char *funcname,
	const bool do_sync)
{
	int rc, ret, fd, temp_dir_fd = -1, mounts_max;
	char oldpath[PATH_MAX], tmp_newpath[PATH_MAX];
	size_t oldpathlen;
	bool symlink_func = (linkfunc == symlink);
	char *mnts[MOUNTS_MAX];
	double t_start, duration, rate, link_count = 0.0;
	char dir_path[PATH_MAX];

	(void)shim_memset(tmp_newpath, 0, sizeof(tmp_newpath));
	(void)snprintf(tmp_newpath, sizeof(tmp_newpath),
		"/tmp/stress-ng-%s-%d-%" PRIu64 "-link",
		args->name, (int)getpid(), stress_mwc64());

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_temp_dir(dir_path, sizeof(dir_path), args->name, args->pid, args->instance);
#if defined(O_DIRECTORY)
	if (do_sync)
		temp_dir_fd = open(dir_path, O_RDONLY | O_DIRECTORY);
#else
	(void)dir_path;
	(void)do_sync;
#endif

	(void)stress_temp_filename_args(args, oldpath, sizeof(oldpath), ~0UL);
	if ((fd = open(oldpath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC)) {
			if (temp_dir_fd >= 0)
				(void)close(temp_dir_fd);
			return EXIT_NO_RESOURCE;
		}
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, oldpath, errno, strerror(errno));
		if (temp_dir_fd >= 0)
			(void)close(temp_dir_fd);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_FAILURE;
	}
	(void)close(fd);

	mounts_max = stress_mount_get(mnts, MOUNTS_MAX);
	oldpathlen = strlen(oldpath);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = EXIT_SUCCESS;
	t_start = stress_time_now();
	do {
		uint64_t i, n = DEFAULT_LINKS;
		char testpath[PATH_MAX];
		ssize_t rret;

		for (i = 0; LIKELY(stress_continue(args) && (i < n)); i++) {
			char newpath[PATH_MAX];
			struct stat stbuf;

			(void)stress_temp_filename_args(args,
				newpath, sizeof(newpath), i);
			if (linkfunc(oldpath, newpath) < 0) {
				if ((errno == EDQUOT) ||
				    (errno == ENOMEM) ||
				    (errno == EMLINK) ||
				    (errno == EINTR) ||
				    (errno == ENOSPC)) {
					/* Try again */
					continue;
				}
				if (errno == EPERM) {
					pr_inf_skip("%s: link calls not allowed on "
						"the filesystem, skipping "
						"stressor\n", args->name);
					rc = EXIT_NO_RESOURCE;
					goto err_unlink;
				}
				rc = stress_exit_status(errno);
				pr_fail("%s: %s failed, errno=%d (%s)%s\n",
					args->name, funcname, errno, strerror(errno),
					stress_get_fs_type(oldpath));
				n = i;
				break;
			} else {
				link_count++;
			}

			if (symlink_func) {
				char buf[PATH_MAX];
#if defined(O_DIRECTORY) &&	\
    defined(HAVE_READLINKAT)
				{
					char tmpfilename[PATH_MAX], *filename;
					char tmpdir[PATH_MAX];
					const char *dir;
					int dir_fd;

					(void)shim_strscpy(tmpfilename, newpath, sizeof(tmpfilename));
					filename = basename(tmpfilename);
					(void)shim_strscpy(tmpdir, newpath, sizeof(tmpdir));
					dir = dirname(tmpdir);

					/*
					 *   Relatively naive readlinkat exercising
					 */
					dir_fd = open(dir, O_DIRECTORY | O_RDONLY);
					if (dir_fd >= 0) {
						rret = readlinkat(dir_fd, filename, buf, sizeof(buf) - 1);
						if ((rret < 0) && (errno != ENOSYS)) {
							pr_fail("%s: readlinkat failed, errno=%d (%s)%s\n",
							args->name, errno, strerror(errno),
							stress_get_fs_type(filename));
							rc = EXIT_FAILURE;
						}
						(void)close(dir_fd);
					}
				}
#endif

				rret = shim_readlink(newpath, buf, sizeof(buf) - 1);
				if (rret < 0) {
					rc = stress_exit_status(errno);
					pr_fail("%s: readlink failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno),
						stress_get_fs_type(newpath));
				} else {
					buf[rret] = '\0';
					if ((size_t)rret != oldpathlen) {
						pr_fail("%s: readlink length error, got %zd, expected: %zd\n",
							args->name, (size_t)rret, oldpathlen);
						rc = EXIT_FAILURE;
					} else {
						if (strncmp(oldpath, buf, (size_t)rret))
							pr_fail("%s: readlink path error, got %s, expected %s\n",
								args->name, buf, oldpath);
					}
				}
			} else {
				/* Hard link, exercise illegal cross device link, EXDEV error */
				if (mounts_max > 0) {
					/* Try hard link on different random mount point */
					const size_t idx = random_mount(mounts_max);

					ret = linkfunc(mnts[idx], tmp_newpath);
					if (ret == 0)
						(void)shim_unlink(tmp_newpath);
				}
			}
			if (shim_lstat(newpath, &stbuf) < 0) {
				rc = stress_exit_status(errno);
				pr_fail("%s: lstat failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(newpath));
			}
#if defined(O_DIRECTORY)
			if (temp_dir_fd > 0)
				(void)fsync(temp_dir_fd);
#endif
		}

#if defined(HAVE_PATHCONF)
#if defined(_PC_LINK_MAX)
		/* exercise pathconf maximum file link count */
		VOID_RET(long int, pathconf(oldpath, _PC_LINK_MAX));
#endif
#if defined(_PC_SYMLINK_MAX)
		/* exercise pathconf maximum file symlink count */
		VOID_RET(long int, pathconf(oldpath, _PC_SYMLINK_MAX));
#endif
#if defined(_PC_2_SYMLINKS)
		/* exercise pathconf maximum file symlink count */
		VOID_RET(long int, pathconf(dir_path, _PC_2_SYMLINKS));
#endif
#endif

		/* exercise invalid newpath size, EINVAL */
		VOID_RET(ssize_t, readlink(oldpath, testpath, 0));

		/* exercise empty oldpath, ENOENT */
		VOID_RET(ssize_t, readlink("", testpath, sizeof(testpath)));

		/* exercise non-link, EINVAL */
		VOID_RET(ssize_t, readlink("/", testpath, sizeof(testpath)));

#if defined(HAVE_READLINKAT) && 	\
    defined(AT_FDCWD)
		/* exercise invalid newpath size, EINVAL */
		VOID_RET(ssize_t, readlinkat(AT_FDCWD, ".", testpath, 0));

		/* exercise invalid newpath size, EINVAL */
		VOID_RET(ssize_t, readlinkat(AT_FDCWD, "", testpath, sizeof(testpath)));

		/* exercise non-link, EINVAL */
		VOID_RET(ssize_t, readlinkat(AT_FDCWD, "/", testpath, sizeof(testpath)));
#endif

err_unlink:
		/* time to finish, indicate so while the slow unlink occurs */
		if (UNLIKELY(!stress_continue(args)))
			stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(O_DIRECTORY)
		if (temp_dir_fd > 0)
			fsync(temp_dir_fd);
#endif
		stress_link_unlink(args, n);
#if defined(O_DIRECTORY)
		if (temp_dir_fd > 0)
			fsync(temp_dir_fd);
#endif
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	duration = stress_time_now() - t_start;
	rate = (duration > 0.0) ? link_count / duration : 0.0;
	stress_metrics_set(args, 0, "links created/removed per sec", rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(O_DIRECTORY)
	if (temp_dir_fd >= 0)
		(void)close(temp_dir_fd);
#endif
	(void)shim_unlink(oldpath);
	(void)stress_temp_dir_rm_args(args);

	stress_mount_free(mnts, mounts_max);

	return rc;
}

#if !defined(__HAIKU__)
/*
 *  stress_link
 *	stress hard links
 */
static int stress_link(stress_args_t *args)
{
	bool link_sync = false;

	(void)stress_get_setting("link-sync", &link_sync);
	return stress_link_generic(args, link, "link", link_sync);
}
#endif

/*
 *  stress_symlink
 *	stress symbolic links
 */
static int stress_symlink(stress_args_t *args)
{
	bool symlink_sync = false;

	(void)stress_get_setting("symlink-sync", &symlink_sync);
	return stress_link_generic(args, symlink, "symlink", symlink_sync);
}

#if !defined(__HAIKU__)
const stressor_info_t stress_link_info = {
	.stressor = stress_link,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = hardlink_help
};
#else
const stressor_info_t stress_link_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = hardlink_help,
	.unimplemented_reason = "unsupported on Haiku"
};
#endif

const stressor_info_t stress_symlink_info = {
	.stressor = stress_symlink,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = symlink_help,
};
