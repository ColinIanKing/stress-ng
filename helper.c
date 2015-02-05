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
 *  stress_get_pagesize()
 *	get pagesize
 */
size_t stress_get_pagesize(void)
{
#ifdef _SC_PAGESIZE
	long sz;
#endif
	static size_t page_size = 0;

	if (page_size > 0)
		return page_size;

#ifdef _SC_PAGESIZE
        sz = sysconf(_SC_PAGESIZE);
	page_size = (sz <= 0) ? PAGE_4K : (size_t)sz;
#else
        page_size = PAGE_4K;
#endif

	return page_size;
}

/*
 *  stress_get_processors_online()
 *	get number of processors that are online
 */
long stress_get_processors_online(void)
{
	static long processors_online = 0;

	if (processors_online > 0)
		return processors_online;

#ifdef _SC_NPROCESSORS_ONLN
	processors_online = sysconf(_SC_NPROCESSORS_ONLN);
	return processors_online;
#else
	return -1;
#endif
}

/*
 *  stress_get_ticks_per_second()
 *	get number of processors that are online
 */
long stress_get_ticks_per_second(void)
{
	static long ticks_per_second = 0;

	if (ticks_per_second > 0)
		return ticks_per_second;

#ifdef _SC_CLK_TCK
	ticks_per_second = sysconf(_SC_CLK_TCK);
	return ticks_per_second;
#else
	return -1;
#endif
}

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

/*
 *  uint64_zero()
 *	return uint64 zero in way that force less smart
 *	static analysers to realise we are doing this
 *	to force a division by zero. I'd like to have
 *	a better solution than this ghastly way.
 */
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

/*
 *  stress_temp_dir()
 *	create a temporary directory name
 */
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

/*
 *   stress_temp_dir_mk()
 *	create a temportary directory
 */
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

/*
 *  stress_temp_dir_rm()
 *	remove a temporary directory
 */
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
