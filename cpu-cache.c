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

#include "stress-ng.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>

#if defined(__linux__)

static inline uint64_t stress_scale_size(
	const uint64_t size,
	const char scale)
{
	switch (scale) {
	case 'K':
		return size * KB;
	case 'M':
		return size * MB;
	case 'G':
		return size * GB;
	default:
		return size;
	}
}

void stress_get_cache_size(uint64_t *l2, uint64_t *l3)
{
	DIR *dp;
	struct dirent *d;
	static const char *path = "/sys/devices/system/cpu/cpu0/cache";

	*l2 = 0ULL;
	*l3 = 0ULL;

	dp = opendir(path);
	if (!dp)
		return;
	while ((d = readdir(dp)) != NULL) {
		char name[PATH_MAX];
		FILE *fp;
		int ret, level;
		char scale;
		uint64_t size;

                if (strncmp(d->d_name, "index", 5))
			continue;
		snprintf(name, sizeof(name), "%s/%s/level", path, d->d_name);
		if ((fp = fopen(name, "r")) == NULL)
			continue;

		ret = fscanf(fp, "%d\n", &level);
		fclose(fp);

		if ((ret != 1) || (level < 1) || (level > 3))
			continue;

		snprintf(name, sizeof(name), "%s/%s/size", path, d->d_name);
		if ((fp = fopen(name, "r")) == NULL)
			continue;

		ret = fscanf(fp, "%" SCNu64 "%c\n", &size, &scale);
		fclose(fp);
		if (ret < 1)
			continue;
		if (ret == 2)
			size = stress_scale_size(size, scale);

		switch (level) {
		case 2:
			*l2 = size;
			break;
		case 3:
			*l3 = size;
			break;
		default:
			break;
		}
	}
	closedir(dp);
}

#else
void stress_get_cache_size(uint64_t *l2, uint64_t *l3)
{
	*l2 = 0;
	*l3 = 0;
}
#endif
