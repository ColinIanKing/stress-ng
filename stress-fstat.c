/*
 * Copyright (C) 2013-2014 Canonical, Ltd.
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
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

/*
 *  stress_fstat()
 *	stress system with fstat
 */
int stress_fstat(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	typedef struct dir_info {
		char	*path;
		struct dir_info *next;
	} dir_info_t;

	DIR *dp;
	dir_info_t *dir_info = NULL, *di;
	struct dirent *d;

	(void)instance;

	if ((dp = opendir(opt_fstat_dir)) == NULL) {
		pr_err(stderr, "%s: opendir on %s failed: errno=%d: (%s)\n",
			name, opt_fstat_dir, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	/* Cache all the directory entries */
	while ((d = readdir(dp)) != NULL) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "%s/%s", opt_fstat_dir, d->d_name);
		if ((di = calloc(1, sizeof(*di))) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			closedir(dp);
			return EXIT_FAILURE;
		}
		if ((di->path = strdup(path)) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			closedir(dp);
			return EXIT_FAILURE;
		}
		di->next = dir_info;
		dir_info = di;
	}
	closedir(dp);

	do {
		struct stat buf;

		for (di = dir_info; di; di = di->next) {
			/* We don't care about it failing */
			(void)stat(di->path, &buf);
			(*counter)++;
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/* Free cache */
	for (di = dir_info; di; ) {
		dir_info_t *next = di->next;

		free(di->path);
		free(di);
		di = next;
	}

	return EXIT_SUCCESS;
}
