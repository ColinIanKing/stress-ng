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

#if defined(STRESS_QUOTA)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/quota.h>
#include <errno.h>
#include <signal.h>

#define MAX_DEVS	(128)

typedef struct {
	char	*name;
	char	*mount;
	dev_t	st_dev;
} dev_info_t;

/*
 *  do_quota()
 */
static void do_quota(const dev_info_t *dev)
{
#if defined(Q_GETQUOTA)
	if (opt_do_run) {
		struct dqblk dqblk;
		(void)quotactl(QCMD(Q_GETQUOTA, USRQUOTA),
			dev->name, 0, (caddr_t)&dqblk);
	}
#endif
#if defined(Q_GETFMT)
	if (opt_do_run) {
		uint32_t format;
		(void)quotactl(QCMD(Q_GETFMT, USRQUOTA),
			dev->name, 0, (caddr_t)&format);
	}
#endif
#if defined(Q_GETINFO)
	if (opt_do_run) {
		struct dqinfo dqinfo;
		(void)quotactl(QCMD(Q_GETINFO, USRQUOTA),
			dev->name, 0, (caddr_t)&dqinfo);
	}
#endif
#if defined(Q_GETSTATS)
	if (opt_do_run) {
		struct dqstats dqstats;
		(void)quotactl(QCMD(Q_GETSTATS, USRQUOTA),
			dev->name, 0, (caddr_t)&dqstats);
	}
#endif
#if defined(Q_SYNC)
	if (opt_do_run) {
		(void)quotactl(QCMD(Q_SYNC, USRQUOTA),
			dev->name, 0, 0);
	}
#endif
}

/*
 *  stress_quota
 *	stress various quota options
 */
int stress_quota(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
        int i, n_mounts, n_devs = 0;
        char *mnts[MAX_DEVS];
	dev_info_t devs[MAX_DEVS];
	DIR *dir;
	struct dirent *d;
	struct stat buf;

	(void)instance;

	memset(mnts, 0, sizeof(mnts));
	memset(devs, 0, sizeof(devs));

	n_mounts = mount_get(mnts, SIZEOF_ARRAY(mnts));

	dir = opendir("/dev/");
	if (!dir) {
		pr_err(stderr, "%s: opendir on /dev failed: errno=%d: (%s)\n",
			name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	for (i = 0; i < n_mounts; i++) {
		lstat(mnts[i], &buf);
		devs[i].st_dev = buf.st_dev;
	}

	while ((d = readdir(dir)) != NULL) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "/dev/%s", d->d_name);
		if (lstat(path, &buf) < 0)
			continue;
		if ((buf.st_mode & S_IFBLK) == 0)
			continue;
		for (i = 0; i < n_mounts; i++) {
			if (!devs[i].name && (buf.st_rdev == devs[i].st_dev)) {
				devs[i].name = strdup(path);
				devs[i].mount = mnts[i];
				if (!devs[i].name) {
					pr_err(stderr, "%s: out of memory\n", name);
					goto tidy;
				}
			}
		}
	}
	closedir(dir);

	/* Compact up, remove duplicates too */
	for (i = 0; i < n_mounts; i++) {
		int j;
		bool unique = true;

		for (j = 0; j < n_devs; j++) {
			if (devs[i].st_dev == devs[j].st_dev) {
				unique = false;
				break;
			}
		}
		if (unique && devs[i].name)
			devs[n_devs++] = devs[i];
		else
			free(devs[i].name);
	}
	for (i = n_devs; i < n_mounts; i++)
		memset(&devs[i], 0, sizeof(devs[i]));

	if (!n_devs) {
		pr_err(stderr, "%s: cannot find any canditate block devices\n", name);
	} else {
		do {
			for (i = 0; opt_do_run && (i < n_devs); i++)
				do_quota(&devs[i]);
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}

	for (i = 0; i < n_devs; i++)
		free(devs[i].name);

	mount_free(mnts, n_mounts);
tidy:
	return EXIT_SUCCESS;
}

#endif
