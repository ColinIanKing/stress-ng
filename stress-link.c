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

/*
 *  stress_link_unlink()
 *	remove all links
 */
static void stress_link_unlink(
	args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];

		(void)stress_temp_filename(path, sizeof(path),
			args->name, args->pid, args->instance, i);
		(void)unlink(path);
	}
	sync();
}

/*
 *  stress_link_generic
 *	stress links, generic case
 */
static int stress_link_generic(
	args_t *args,
	int (*linkfunc)(const char *oldpath, const char *newpath),
	const char *funcname,
	bool symlink)
{
	int rc, ret, fd;
	char oldpath[PATH_MAX];
	size_t oldpathlen;

	ret = stress_temp_dir_mk(args->name, args->pid, args->instance);
	if (ret < 0)
		return exit_status(-ret);
	(void)stress_temp_filename(oldpath, sizeof(oldpath),
		args->name, args->pid, args->instance, ~0);
	if ((fd = open(oldpath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_fail_err(args->name, "open");
		(void)stress_temp_dir_rm(args->name, args->pid, args->instance);
		return ret;
	}
	(void)close(fd);

	oldpathlen = strlen(oldpath);

	rc = EXIT_SUCCESS;
	do {
		uint64_t i, n = DEFAULT_LINKS;

		for (i = 0; i < n; i++) {
			char newpath[PATH_MAX];
			struct stat stbuf;

			(void)stress_temp_filename(newpath, sizeof(newpath),
				args->name, args->pid, args->instance, i);
			if (linkfunc(oldpath, newpath) < 0) {
				rc = exit_status(errno);
				pr_fail_err(args->name, funcname);
				n = i;
				break;
			}
			if (symlink) {
				char buf[PATH_MAX];
				ssize_t rret;

				rret = readlink(newpath, buf, sizeof(buf) - 1);
				if (rret < 0) {
					rc = exit_status(errno);
					pr_fail_err(args->name, "readlink");
				} else {
					newpath[rret] = '\0';
					if ((size_t)rret != oldpathlen)
						pr_fail_err(args->name, "readlink length error");
					else
						if (strncmp(oldpath, buf, rret))
							pr_fail_err(args->name, "readlink path error");
				}
			}
			if (lstat(newpath, &stbuf) < 0) {
				rc = exit_status(errno);
				pr_fail_err(args->name, "lstat");
			}

			if (!opt_do_run ||
			    (args->max_ops && *args->counter >= args->max_ops))
				goto abort;

			inc_counter(args);
		}
		stress_link_unlink(args, n);
	} while (opt_do_run && (!args->max_ops || *args->counter < args->max_ops));

abort:
	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing %" PRIu32" entries\n", args->name, DEFAULT_LINKS);
	stress_link_unlink(args, DEFAULT_LINKS);
	(void)unlink(oldpath);
	(void)stress_temp_dir_rm(args->name, args->pid, args->instance);

	return rc;
}

/*
 *  stress_link
 *	stress hard links
 */
int stress_link(args_t *args)
{
	return stress_link_generic(args, link, "link", false);
}

/*
 *  stress_symlink
 *	stress symbolic links
 */
int stress_symlink(args_t *args)
{
	return stress_link_generic(args, symlink, "symlink", true);
}
