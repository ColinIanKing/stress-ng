/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-mounts.h"

/* *BSD systems */
#if defined(HAVE_SYS_UCRED_H)
#include <sys/ucred.h>
#endif

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

#if defined(HAVE_MNTENT_H)
#include <mntent.h>
#endif

/*
 *  stress_mount_add()
 *	add a new mount point to table
 */
static void stress_mount_add(
	char *mnts[],
	const int max,
	int *n,
	const char *name)
{
	char *mnt;

	if (*n >= max)
		return;
	mnt = shim_strdup(name);
	if (UNLIKELY(!mnt))
		return;
	mnts[*n] = mnt;
	(*n)++;
}

/*
 *  stress_mount_free()
 *	free mount info
 */
void stress_mount_free(char *mnts[], const int n)
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
int stress_mount_get(char *mnts[], const int max)
{
	int i, n = 0, ret;
#if defined(__NetBSD__) || defined(__minix__)
	struct statvfs *statbufs;
#else
	struct statfs *statbufs;
#endif

	(void)shim_memset(mnts, 0, max * sizeof(*mnts));
	ret = getmntinfo(&statbufs, 0);
	if (ret > max)
		ret = max;

	for (i = 0; i < ret; i++) {
		stress_mount_add(mnts, max, &n, statbufs[i].f_mntonname);
	}
	return ret;
}
#elif defined(HAVE_GETMNTENT) &&	\
      defined(HAVE_MNTENT_H)
int stress_mount_get(char *mnts[], const int max)
{
	FILE *mounts;
	const struct mntent* mnt;
	int n = 0;

	(void)shim_memset(mnts, 0, (size_t)max * sizeof(*mnts));
	mounts = setmntent("/etc/mtab", "r");
	/* Failed, so assume / is available */
	if (!mounts) {
		stress_mount_add(mnts, max, &n, "/");
		return n;
	}
	while ((mnt = getmntent(mounts)) != NULL)
		stress_mount_add(mnts, max, &n, mnt->mnt_dir);

	(void)endmntent(mounts);
	return n;
}
#else
int stress_mount_get(char *mnts[], const int max)
{
	int n = 0;

	(void)shim_memset(mnts, 0, max * sizeof(*mnts));
	stress_mount_add(mnts, max, &n, "/");
	stress_mount_add(mnts, max, &n, "/dev");
	stress_mount_add(mnts, max, &n, "/tmp");

	return n;
}
#endif
