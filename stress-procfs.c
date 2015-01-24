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
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "stress-ng.h"

#define PROC_BUF_SZ	(4096)

#if defined(__linux__)
/*
 *  stress_proc_read()
 *	read a proc file
 */
static inline void stress_proc_read(const char *path)
{
	int fd, i;
	char buffer[PROC_BUF_SZ];

	if ((fd = open(path, O_RDONLY)) < 0)
		return;

	/* Limit to 4K * 4K of data to read per file */
	for (i = 0; i < 4096; i++) {
		if (!opt_do_run)
			break;
		if (read(fd, buffer, PROC_BUF_SZ) < PROC_BUF_SZ)
			break;
	}
	(void)close(fd);
}

/*
 *  stress_proc_dir()
 *	read directory
 */
static void stress_proc_dir(
	const char *path,
	const bool recurse,
	const int depth)
{
	DIR *dp;
	struct dirent *d;

	if (!opt_do_run)
		return;

	/* Don't want to go too deep */
	if (depth > 8)
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
				stress_proc_dir(name, recurse, depth + 1);
			}
			break;
		case DT_REG:
			snprintf(name, sizeof(name),
				"%s/%s", path, d->d_name);
			stress_proc_read(name);
			break;
		default:
			break;
		}
	}
	(void)closedir(dp);
}

/*
 *  stress_procfs
 *	stress reading all of /proc
 */
int stress_procfs(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	do {
		stress_proc_dir("/proc/self", true, 0);
		if (!opt_do_run)
			break;
		stress_proc_dir("/proc", false, 0);
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	return EXIT_SUCCESS;
}
#endif
