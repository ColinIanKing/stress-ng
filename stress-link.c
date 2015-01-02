/*
 * Copyright (C) 2013-2015 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <unistd.h>
#include <limits.h>
#include <fcntl.h>

#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

/*
 *  stress_link_unlink()
 *	remove all links
 */
static void stress_link_unlink(
	const uint64_t n,
	const char *name,
	const pid_t pid,
	const uint32_t instance)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];

		(void)stress_temp_filename(path, sizeof(path),
			name, pid, instance, i);
		(void)unlink(path);
	}
	sync();
}

/*
 *  stress_link_generic
 *	stress links, generic case
 */
static int stress_link_generic(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name,
	int (*linkfunc)(const char *oldpath, const char *newpath),
	const char *funcname)
{
	const pid_t pid = getpid();
	int fd;
	char oldpath[PATH_MAX];

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;
	(void)stress_temp_filename(oldpath, sizeof(oldpath),
		name, pid, instance, ~0);
	if ((fd = open(oldpath, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		(void)stress_temp_dir_rm(name, pid, instance);
		return EXIT_FAILURE;
	}
	(void)close(fd);

	do {
		uint64_t i, n = DEFAULT_LINKS;

		for (i = 0; i < n; i++) {
			char newpath[PATH_MAX];

			(void)stress_temp_filename(newpath, sizeof(newpath),
				name, pid, instance, i);
			if (linkfunc(oldpath, newpath) < 0) {
				pr_failed_err(name, funcname);
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_link_unlink(n, name, pid, instance);
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing %" PRIu32" entries\n", name, DEFAULT_LINKS);
	stress_link_unlink(DEFAULT_LINKS, name, pid, instance);
	(void)unlink(oldpath);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}

/*
 *  stress_link
 *	stress hard links
 */
int stress_link(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_link_generic(counter, instance,
		max_ops, name, link, "link");
}

/*
 *  stress_symlink
 *	stress symbolic links
 */
int stress_symlink(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	return stress_link_generic(counter, instance,
		max_ops, name, symlink, "symlink");
}
