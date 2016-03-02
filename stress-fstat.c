/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "stress-ng.h"

static const char *opt_fstat_dir = "/dev";

void stress_set_fstat_dir(const char *optarg)
{
	opt_fstat_dir = optarg;
}

static const char *blacklist[] = {
	"/dev/watchdog"
};

/*
 *  do_not_stat()
 *	Check if file should not be stat'd
 */
static bool do_not_stat(const char *filename)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(blacklist); i++) {
		if (!strncmp(filename, blacklist[i], strlen(blacklist[i])))
			return true;
	}
	return false;
}

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
		bool	ignore;
		bool	noaccess;
		struct dir_info *next;
	} dir_info_t;

	DIR *dp;
	dir_info_t *dir_info = NULL, *di;
	struct dirent *d;
	int ret = EXIT_FAILURE;
	bool stat_some;

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
		if (do_not_stat(path))
			continue;
		if ((di = calloc(1, sizeof(*di))) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			(void)closedir(dp);
			goto free_cache;
		}
		if ((di->path = strdup(path)) == NULL) {
			pr_err(stderr, "%s: out of memory\n", name);
			free(di);
			(void)closedir(dp);
			goto free_cache;
		}
		di->ignore = false;
		di->noaccess = false;
		di->next = dir_info;
		dir_info = di;
	}
	(void)closedir(dp);

	do {
		stat_some = false;

		for (di = dir_info; di; di = di->next) {
			int fd;
			struct stat buf;

			if (di->ignore)
				continue;

			if ((stat(di->path, &buf) < 0) &&
			    (errno != ENOMEM)) {
				di->ignore = true;
				continue;
			}
			if ((lstat(di->path, &buf) < 0) &&
			    (errno != ENOMEM)) {
				di->ignore = true;
				continue;
			}
			if (di->noaccess)
				continue;

			fd = open(di->path, O_RDONLY | O_NONBLOCK);
			if (fd < 0) {
				di->noaccess = true;
				continue;
			}

			if ((fstat(fd, &buf) < 0) &&
			    (errno != ENOMEM))
				di->ignore = true;
			(void)close(fd);

			stat_some = true;

			(*counter)++;
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				break;
		}
	} while (stat_some && opt_do_run && (!max_ops || *counter < max_ops));

	ret = EXIT_SUCCESS;
free_cache:
	/* Free cache */
	for (di = dir_info; di; ) {
		dir_info_t *next = di->next;

		free(di->path);
		free(di);
		di = next;
	}

	return ret;
}
