/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t hardlink_help[] = {
	{ NULL,	"link N",	 "start N workers creating hard links" },
	{ NULL,	"link-ops N",	 "stop after N link bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static const stress_help_t symlink_help[] = {
	{ NULL, "symlink N",	 "start N workers creating symbolic links" },
	{ NULL, "symlink-ops N", "stop after N symbolic link bogo operations" },
	{ NULL, NULL,            NULL }
};

#define MOUNTS_MAX	(128)

/*
 *  stress_link_unlink()
 *	remove all links
 */
static void stress_link_unlink(
	const stress_args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];

		(void)stress_temp_filename_args(args,
			path, sizeof(path), i);
		(void)unlink(path);
	}
	(void)sync();
}

/*
 *  stress_link_generic
 *	stress links, generic case
 */
static int stress_link_generic(
	const stress_args_t *args,
	int (*linkfunc)(const char *oldpath, const char *newpath),
	const char *funcname)
{
	int rc, ret, fd, mounts_max;
	char oldpath[PATH_MAX], tmp_newpath[PATH_MAX];
	size_t oldpathlen;
	bool symlink_func = (linkfunc == symlink);
	char *mnts[MOUNTS_MAX];

	(void)snprintf(tmp_newpath, sizeof(tmp_newpath),
		"/tmp/stress-ng-%s-%d-%" PRIu64 "-link",
		args->name, getpid(), stress_mwc64());

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename_args(args, oldpath, sizeof(oldpath), ~0);
	if ((fd = open(oldpath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		if ((errno == ENFILE) || (errno == ENOMEM) || (errno == ENOSPC))
			return EXIT_NO_RESOURCE;
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, oldpath, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return EXIT_FAILURE;
	}
	(void)close(fd);

	mounts_max = stress_mount_get(mnts, MOUNTS_MAX);

	oldpathlen = strlen(oldpath);

	rc = EXIT_SUCCESS;
	do {
		uint64_t i, n = DEFAULT_LINKS;
		char testpath[PATH_MAX];

		for (i = 0; i < n; i++) {
			char newpath[PATH_MAX];
			struct stat stbuf;

			(void)stress_temp_filename_args(args,
				newpath, sizeof(newpath), i);
			if (linkfunc(oldpath, newpath) < 0) {
				if ((errno == EDQUOT) ||
				    (errno == ENOMEM) ||
				    (errno == ENOSPC)) {
					/* Try again */
					continue;
				}
				rc = exit_status(errno);
				pr_fail("%s: %s failed, errno=%d (%s)\n",
					args->name, funcname, errno, strerror(errno));
				n = i;
				break;
			}

			if (symlink_func) {
				char buf[PATH_MAX];
				ssize_t rret;
#if defined(O_DIRECTORY) &&	\
    defined(HAVE_READLINKAT)
				{
					char tmpfile[PATH_MAX], *filename;
					char tmpdir[PATH_MAX], *dir;
					int dirfd;

					shim_strlcpy(tmpfile, newpath, sizeof(tmpfile));
					filename = basename(tmpfile);
					shim_strlcpy(tmpdir, newpath, sizeof(tmpdir));
					dir = dirname(tmpdir);

					/*
					 *   Relatively naive readlinkat exercising
					 */
					dirfd = open(dir, O_DIRECTORY | O_RDONLY);
					if (dirfd >= 0) {
						rret = readlinkat(dirfd, filename, buf, sizeof(buf) - 1);
						if ((rret < 0) && (errno != ENOSYS)) {
							pr_fail("%s: readlinkat failed, errno=%d (%s)\n",
							args->name, errno, strerror(errno));
						}
						(void)close(dirfd);
					}
				}
#endif

				rret = shim_readlink(newpath, buf, sizeof(buf) - 1);
				if (rret < 0) {
					rc = exit_status(errno);
					pr_fail("%s: readlink failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
				} else {
					newpath[rret] = '\0';
					if ((size_t)rret != oldpathlen)
						pr_fail("%s: readlink length error, got %zd, expected: %zd\n",
							args->name, (size_t)rret, oldpathlen);
					else
						if (strncmp(oldpath, buf, rret))
							pr_fail("%s: readlink path error, got %s, expected %s\n",
								args->name, buf, oldpath);
				}
			} else {
				/* Hard link, exercise illegal cross device link, EXDEV error */
				if (mounts_max > 0) {
					/* Try hard link on differet random mount point */
					ret = linkfunc(mnts[stress_mwc32() % mounts_max], tmp_newpath);
					if (ret == 0)
						(void)unlink(tmp_newpath);
				}
			}
			if (lstat(newpath, &stbuf) < 0) {
				rc = exit_status(errno);
				pr_fail("%s: lstat failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}

		/* exercise invalid newpath size, EINVAL */
		ret = readlink(oldpath, testpath, 0);
		(void)ret;

		/* exercise empty oldpath, ENOENT */
		ret = readlink("", testpath, sizeof(testpath));
		(void)ret;

		/* exercise non-link, EINVAL */
		ret = readlink("/", testpath, sizeof(testpath));
		(void)ret;

#if defined(HAVE_READLINKAT) && 	\
    defined(AT_FDCWD)
		/* exercise invalid newpath size, EINVAL */
		ret = readlinkat(AT_FDCWD, ".", testpath, 0);
		(void)ret;

		/* exercise invalid newpath size, EINVAL */
		ret = readlinkat(AT_FDCWD, "", testpath, sizeof(testpath));
		(void)ret;

		/* exercise non-link, EINVAL */
		ret = readlinkat(AT_FDCWD, "/", testpath, sizeof(testpath));
		(void)ret;

#endif

		stress_link_unlink(args, n);

		inc_counter(args);
	} while (keep_stressing());

	(void)unlink(oldpath);
	(void)stress_temp_dir_rm_args(args);

	stress_mount_free(mnts, mounts_max);

	return rc;
}

#if !defined(__HAIKU__)
/*
 *  stress_link
 *	stress hard links
 */
static int stress_link(const stress_args_t *args)
{
	return stress_link_generic(args, link, "link");
}
#endif

/*
 *  stress_symlink
 *	stress symbolic links
 */
static int stress_symlink(const stress_args_t *args)
{
	return stress_link_generic(args, symlink, "symlink");
}

#if !defined(__HAIKU__)
stressor_info_t stress_link_info = {
	.stressor = stress_link,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = hardlink_help
};
#else
stressor_info_t stress_link_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = hardlink_help
};
#endif

stressor_info_t stress_symlink_info = {
	.stressor = stress_symlink,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = symlink_help
};
