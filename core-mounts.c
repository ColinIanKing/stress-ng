/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 *  mount_add()
 *	add a new mount point to table
 */
static void mount_add(
	char *mnts[],
	const int max,
	int *n,
	const char *name)
{
	char *mnt;

	if (*n >= max)
		return;
	mnt = strdup(name);
	if (!mnt)
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
#if defined(HAVE_GETMNTINFO)
int mount_get(char *mnts[], const int max)
{
	int i, n = 0, ret;
#if defined(__NetBSD__) || defined(__minix__)
	struct statvfs *statbufs;
#else
	struct statfs *statbufs;
#endif

	ret = getmntinfo(&statbufs, 0);
	if (ret > max)
		ret = max;

	for (i = 0; i < ret; i++) {
		mount_add(mnts, max, &n, statbufs[i].f_mntonname);
	}
	return ret;
}
#elif defined(HAVE_GETMNTENT) && defined(HAVE_MNTENT_H)
int mount_get(char *mnts[], const int max)
{
	FILE *mounts;
	struct mntent* mnt;
	int n = 0;

	mounts = setmntent("/etc/mtab", "r");
	/* Failed, so assume / is available */
	if (!mounts) {
		mount_add(mnts, max, &n, "/");
		return n;
	}
	while ((mnt = getmntent(mounts)) != NULL)
		mount_add(mnts, max, &n, mnt->mnt_dir);

	(void)endmntent(mounts);
	return n;
}
#else
int mount_get(char *mnts[], const int max)
{
	int n = 0;

	mount_add(mnts, max, &n, "/");
	mount_add(mnts, max, &n, "/dev");
	mount_add(mnts, max, &n, "/tmp");

	return n;
}
#endif
