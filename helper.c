/*
 * Copyright (C) 2014-2015 Canonical, Ltd.
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
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <inttypes.h>
#include <libgen.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "stress-ng.h"

#define MUNGE_MIN(a,b) (((a) < (b)) ? (a) : (b))

/*
 *  munge_underscore()
 *	turn '_' to '-' in strings
 */
char *munge_underscore(char *str)
{
	static char munged[128];
	char *src, *dst;
	size_t str_len = strlen(str);
	ssize_t len = MUNGE_MIN(str_len, sizeof(munged) - 1);

	for (src = str, dst = munged; *src && (dst - munged) < len; src++)
		*dst++ = (*src == '_' ? '-' : *src);

	*dst = '\0';

	return munged;
}

/*
 *  force stress-float to think the doubles are actually
 *  being used - this avoids the float loop from being
 *  over optimised out per iteration.
 */
void double_put(const double a)
{
	(void)a;
}

/*
 *  force stress-int to think the uint64_t args are actually
 *  being used - this avoids the integer loop from being
 *  over optimised out per iteration.
 */
void uint64_put(const uint64_t a)
{
	(void)a;
}

uint64_t uint64_zero(void)
{
	return 0ULL;
}

/*
 *  stress_temp_filename()
 *      construct a temp filename
 */
int stress_temp_filename(
        char *path,
        const size_t len,
        const char *name,
        const pid_t pid,
        const uint32_t instance,
        const uint64_t magic)
{
	return snprintf(path, len, ".%s-%i-%"
		PRIu32 "/%s-%i-%"
                PRIu32 "-%" PRIu64,
                name, pid, instance,
		name, pid, instance, magic);
}

int stress_temp_dir(
	char *path,
        const size_t len,
	const char *name,
        const pid_t pid,
        const uint32_t instance)
{
	return snprintf(path, len, ".%s-%i-%" PRIu32,
		name, pid, instance);
}

int stress_temp_dir_mk(
	const char *name,
        const pid_t pid,
        const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = mkdir(tmp, S_IRWXU);
	if (ret < 0)
		pr_failed_err(name, "mkdir");

	return ret;
}

int stress_temp_dir_rm(
	const char *name,
        const pid_t pid,
        const uint32_t instance)
{
	int ret;
	char tmp[PATH_MAX + 1];

	stress_temp_dir(tmp, sizeof(tmp), name, pid, instance);
	ret = rmdir(tmp);
	if (ret < 0)
		pr_failed_err(name, "rmdir");

	return ret;
}
