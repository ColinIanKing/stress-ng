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
	bool	valid;
} dev_info_t;

#define DO_Q_GETQUOTA	0x0001
#define DO_Q_GETFMT	0x0002
#define DO_Q_GETINFO	0x0004
#define DO_Q_GETSTATS	0x0008
#define DO_Q_SYNC	0x0010

/*
 *  do_quotactl()
 *	do a quotactl command
 */
static int do_quotactl(
	const char *name,
	const int flag,
	const char *cmdname,
	int *tested,
	int *failed,
	int *enosys,
	int cmd,
	const char *special,
	int id,
	caddr_t addr)
{
	static int failed_mask = 0;
	int ret = quotactl(cmd, special, id, addr);

	(*tested)++;
	if (ret < 0) {
		if (errno == EPERM) {
			pr_inf(stderr, "%s: need CAP_SYS_ADMIN capability to "
				"run quota stressor, aborting stress test\n",
				name);
			return errno;
		}
		if ((failed_mask & flag) == 0) {
			/* Just issue the warning once, reduce log spamming */
			failed_mask |= flag;
			pr_fail(stderr, "%s: quotactl command %s failed: errno=%d (%s)\n",
				name, cmdname, errno, strerror(errno));
		}
		if (errno == ENOSYS)
			(*enosys)++;
		else
			(*failed)++;
	}
	return errno;
}

/*
 *  do_quotas()
 *	do quotactl commands
 */
static int do_quotas(const dev_info_t *dev, const char *name)
{
	int tested = 0, failed = 0, enosys = 0;
	int errno;
#if defined(Q_GETQUOTA)
	if (opt_do_run) {
		struct dqblk dqblk;

		errno = do_quotactl(name, DO_Q_GETQUOTA, "Q_GETQUOTA",
			&tested, &failed, &enosys,
			QCMD(Q_GETQUOTA, USRQUOTA),
			dev->name, 0, (caddr_t)&dqblk);
		if (errno == EPERM)
			return errno;
	}
#endif
#if defined(Q_GETFMT)
	if (opt_do_run) {
		uint32_t format;

		errno = do_quotactl(name, DO_Q_GETFMT, "Q_GETFMT",
			&tested, &failed, &enosys,
			QCMD(Q_GETFMT, USRQUOTA),
			dev->name, 0, (caddr_t)&format);
		if (errno == EPERM)
			return errno;
	}
#endif
#if defined(Q_GETINFO)
	if (opt_do_run) {
		struct dqinfo dqinfo;

		errno = do_quotactl(name, DO_Q_GETINFO, "Q_GETINFO",
			&tested, &failed, &enosys,
			QCMD(Q_GETINFO, USRQUOTA),
			dev->name, 0, (caddr_t)&dqinfo);
		if (errno == EPERM)
			return errno;
	}
#endif
#if defined(Q_GETSTATS)
	/* Obsolete in recent kernels */
	if (opt_do_run) {
		struct dqstats dqstats;

		errno = do_quotactl(name, DO_Q_GETSTATS, "Q_GETSTATS",
			&tested, &failed, &enosys,
			QCMD(Q_GETSTATS, USRQUOTA),
			dev->name, 0, (caddr_t)&dqstats);
		if (errno == EPERM)
			return errno;
	}
#endif
#if defined(Q_SYNC)
	if (opt_do_run) {
		errno = do_quotactl(name, DO_Q_SYNC, "Q_SYNC",
			&tested, &failed, &enosys,
			QCMD(Q_SYNC, USRQUOTA),
			dev->name, 0, 0);
		if (errno == EPERM)
			return errno;
	}
#endif
	if (tested == 0) {
		errno = pr_err(stderr, "%s: quotactl() failed, quota commands "
			"not available\n", name);
		return -1;
	}
	if (tested == enosys) {
		pr_err(stderr, "%s: quotactl() failed, not available "
			"on this kernel\n", name);
		return -1;
	}
	if (tested == failed) {
		pr_err(stderr, "%s: quotactl() failed, all quota commands "
			"failed (maybe privilege issues, use -v "
			"to see why)\n", name);
		return -1;
	}
	return 0;
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
	int rc = EXIT_FAILURE;
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
		return rc;
	}

	for (i = 0; i < n_mounts; i++) {
		if (lstat(mnts[i], &buf) == 0) {
			devs[i].st_dev = buf.st_dev;
			devs[i].valid = true;
		}
	}

	while ((d = readdir(dir)) != NULL) {
		char path[PATH_MAX];

		snprintf(path, sizeof(path), "/dev/%s", d->d_name);
		if (lstat(path, &buf) < 0)
			continue;
		if ((buf.st_mode & S_IFBLK) == 0)
			continue;
		for (i = 0; i < n_mounts; i++) {
			if (devs[i].valid &&
			    !devs[i].name &&
			    (buf.st_rdev == devs[i].st_dev)) {
				devs[i].name = strdup(path);
				devs[i].mount = mnts[i];
				if (!devs[i].name) {
					pr_err(stderr, "%s: out of memory\n",
						name);
					closedir(dir);
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
		pr_err(stderr, "%s: cannot find any candidate block "
			"devices with quota enabled\n", name);
	} else {
		do {
			for (i = 0; opt_do_run && (i < n_devs); i++) {
				int ret = do_quotas(&devs[i], name);
				if (ret == EPERM)
					rc = EXIT_SUCCESS;
				if (ret)
					goto tidy;
			}
			(*counter)++;
		} while (opt_do_run && (!max_ops || *counter < max_ops));
	}
	rc = EXIT_SUCCESS;

tidy:
	for (i = 0; i < n_devs; i++)
		free(devs[i].name);

	mount_free(mnts, n_mounts);
	return rc;
}

#endif
