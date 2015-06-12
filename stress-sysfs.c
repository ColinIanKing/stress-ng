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

#include "stress-ng.h"

#if defined(STRESS_SYSFS)

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#define SYS_BUF_SZ	(4096)

/*
 *  stress_sys_read()
 *	read a proc file
 */
static inline void stress_sys_read(const char *path)
{
	int fd, i;
	char buffer[SYS_BUF_SZ];

	if ((fd = open(path, O_RDONLY | O_NONBLOCK)) < 0)
		return;

	/* Limit to 4K * 4K of data to read per file */
	for (i = 0; i < 4096; i++) {
		ssize_t ret;
redo:
		if (!opt_do_run)
			break;

		ret = read(fd, buffer, SYS_BUF_SZ);
		if (ret < 0) {
			if (errno == EINTR)
				goto redo;
			break;
		}
		if (ret < SYS_BUF_SZ)
			break;
	}
	(void)close(fd);
}

/*
 *  stress_sys_dir()
 *	read directory
 */
static void stress_sys_dir(
	const char *path,
	const bool recurse,
	const int depth,
	bool sys_read)
{
	DIR *dp;
	struct dirent *d;

	if (!opt_do_run)
		return;

	/* Don't want to go too deep */
	if (depth > 20)
		return;

	dp = opendir(path);
	if (dp == NULL)
		return;

	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];

		if (!opt_do_run)
			break;
		if (!strcmp(d->d_name, ".") ||
		    !strcmp(d->d_name, ".."))
			continue;
		switch (d->d_type) {
		case DT_DIR:
			if (recurse) {
				snprintf(name, sizeof(name),
					"%s/%s", path, d->d_name);
				stress_sys_dir(name, recurse, depth + 1, sys_read);
			}
			break;
		case DT_REG:
			if (sys_read) {
				snprintf(name, sizeof(name),
					"%s/%s", path, d->d_name);
				stress_sys_read(name);
			}
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  stress_sysfs
 *	stress reading all of /sys
 */
int stress_sysfs(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	bool sys_read = true;

	(void)instance;
	(void)name;


	if (geteuid() == 0) {
		pr_inf(stderr, "%s: running as root, just traversing /sys and not reading files.\n", name);
		sys_read = false;
	}

	do {
		stress_sys_dir("/sys", true, 0, sys_read);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
