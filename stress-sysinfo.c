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

#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#ifndef __FreeBSD__
#include <mntent.h>
#endif
#include <sys/types.h>
#include <sys/time.h>
#include <sys/resource.h>
#if defined (__linux__)
#include <sys/sysinfo.h>
#include <sys/statfs.h>
#endif
#include <sys/times.h>

#include "stress-ng.h"

#define check_do_run()		\
	if (!opt_do_run)	\
		break;		\

/*
 *  mount_add()
 *	add a new mount point to table
 */
void mount_add(
	char *mnts[],
	const int max,
	int *n,
	const char *name)
{
	char *mnt;

	if (*n >= max)
		return;

	mnt = strdup(name);
	if (mnt == NULL)
		return;

	mnts[*n] = mnt;
	(*n)++;
}

/*
 *  mount_free()
 *	free mount info
 */
void mount_free(char *mnts[], const int n)
{
	int i;

	for (i = 0; i < n; i++) {
		free(mnts[i]);
		mnts[i] = NULL;
	}
}

/*
 *  mount_get()
 *	populate mnts with up to max mount points
 *	from /etc/mtab
 */
#ifdef __FreeBSD__
int mount_get(char *mnts[], const int max)
{
	int n = 0;

	mount_add(mnts, max, &n, "/");
	mount_add(mnts, max, &n, "/dev");

	return n;
}
#else
int mount_get(char *mnts[], const int max)
{
	FILE *mounts;
	struct mntent* mount;
	int n = 0;

	mounts = setmntent("/etc/mtab", "r");
	/* Failed, so assume / is available */
	if (mounts == NULL) {
		mount_add(mnts, max, &n, "/");
		return n;
	}
	while ((mount = getmntent(mounts)) != NULL)
		mount_add(mnts, max, &n, mount->mnt_dir);

	(void)endmntent(mounts);

	return n;
}
#endif

/*
 *  stress on system information
 *	stress system by rapid fetches of system information
 */
int stress_sysinfo(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int n_mounts;
	char *mnts[128];

	(void)instance;

	n_mounts = mount_get(mnts, SIZEOF_ARRAY(mnts));
	pr_dbg(stderr, "%s: statfs on %d mount points\n",
		name, n_mounts);

	do {
		struct tms tms_buf;
		clock_t clk;
#if defined (__linux__)
		int ret;
		struct sysinfo sysinfo_buf;
		struct statfs statfs_buf;
		int i;
#endif

#if defined (__linux__)
		ret = sysinfo(&sysinfo_buf);
		if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
			 pr_fail(stderr, "%s: sysinfo failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		check_do_run();

		for (i = 0; i < n_mounts; i++) {
			check_do_run();

			if (!mnts[i])
				continue;

			ret = statfs(mnts[i], &statfs_buf);
			/* Mount may have been removed, so purge it */
			if ((ret < 0) && (errno == ENOENT)) {
				free(mnts[i]);
				mnts[i] = NULL;
				continue;
			}
			if ((ret < 0) && (opt_flags & OPT_FLAGS_VERIFY)) {
				pr_fail(stderr, "%s: statfs on %s failed: errno=%d (%s)\n",
					name, mnts[i], errno, strerror(errno));
			}
		}
		check_do_run();
#endif
		clk = times(&tms_buf);
		if ((clk == (clock_t)-1) && (opt_flags & OPT_FLAGS_VERIFY)) {
			 pr_fail(stderr, "%s: times failed: errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	mount_free(mnts, n_mounts);

	return EXIT_SUCCESS;
}
