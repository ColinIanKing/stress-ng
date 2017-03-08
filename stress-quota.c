/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(__linux__) && (	\
    defined(Q_GETQUOTA) ||	\
    defined(Q_GETFMT) ||	\
    defined(Q_GETINFO) ||	\
    defined(Q_GETSTATS) ||	\
    defined(Q_SYNC))

#include <sys/quota.h>

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
	const args_t *args,
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
	int ret = quotactl(cmd, special, id, addr);

	(*tested)++;
	if (ret < 0) {
		static int failed_mask = 0;

		if (errno == EPERM) {
			pr_inf("%s: need CAP_SYS_ADMIN capability to "
				"run quota stressor, aborting stress test\n",
				args->name);
			return errno;
		}
		if ((failed_mask & flag) == 0) {
			/* Just issue the warning once, reduce log spamming */
			failed_mask |= flag;
			pr_fail("%s: quotactl command %s failed: errno=%d (%s)\n",
				args->name, cmdname, errno, strerror(errno));
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
static int do_quotas(const args_t *args, const dev_info_t *dev)
{
	int tested = 0, failed = 0, enosys = 0;
#if defined(Q_GETQUOTA)
	if (g_keep_stressing_flag) {
		struct dqblk dqblk;
		int err = do_quotactl(args, DO_Q_GETQUOTA, "Q_GETQUOTA",
			&tested, &failed, &enosys,
			QCMD(Q_GETQUOTA, USRQUOTA),
			dev->name, 0, (caddr_t)&dqblk);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETFMT)
	if (g_keep_stressing_flag) {
		uint32_t format;
		int err = do_quotactl(args, DO_Q_GETFMT, "Q_GETFMT",
			&tested, &failed, &enosys,
			QCMD(Q_GETFMT, USRQUOTA),
			dev->name, 0, (caddr_t)&format);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETINFO)
	if (g_keep_stressing_flag) {
		struct dqinfo dqinfo;
		int err = do_quotactl(args, DO_Q_GETINFO, "Q_GETINFO",
			&tested, &failed, &enosys,
			QCMD(Q_GETINFO, USRQUOTA),
			dev->name, 0, (caddr_t)&dqinfo);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETSTATS)
	/* Obsolete in recent kernels */
	if (g_keep_stressing_flag) {
		struct dqstats dqstats;
		int err = do_quotactl(args, DO_Q_GETSTATS, "Q_GETSTATS",
			&tested, &failed, &enosys,
			QCMD(Q_GETSTATS, USRQUOTA),
			dev->name, 0, (caddr_t)&dqstats);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_SYNC)
	if (g_keep_stressing_flag) {
		int err = do_quotactl(args, DO_Q_SYNC, "Q_SYNC",
			&tested, &failed, &enosys,
			QCMD(Q_SYNC, USRQUOTA),
			dev->name, 0, 0);
		if (err == EPERM)
			return err;
	}
#endif
	if (tested == 0) {
		pr_err("%s: quotactl() failed, quota commands "
			"not available\n", args->name);
		return -1;
	}
	if (tested == enosys) {
		pr_err("%s: quotactl() failed, not available "
			"on this kernel\n", args->name);
		return -1;
	}
	if (tested == failed) {
		pr_err("%s: quotactl() failed, all quota commands "
			"failed (maybe privilege issues, use -v "
			"to see why)\n", args->name);
		return -1;
	}
	return 0;
}

/*
 *  stress_quota
 *	stress various quota options
 */
int stress_quota(const args_t *args)
{
	int i, n_mounts, n_devs = 0;
	int rc = EXIT_FAILURE;
	char *mnts[MAX_DEVS];
	dev_info_t devs[MAX_DEVS];
	DIR *dir;
	struct dirent *d;
	struct stat buf;

	memset(mnts, 0, sizeof(mnts));
	memset(devs, 0, sizeof(devs));

	n_mounts = mount_get(mnts, SIZEOF_ARRAY(mnts));

	dir = opendir("/dev/");
	if (!dir) {
		pr_err("%s: opendir on /dev failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
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

		(void)snprintf(path, sizeof(path), "/dev/%s", d->d_name);
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
					pr_err("%s: out of memory\n",
						args->name);
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
		pr_err("%s: cannot find any candidate block "
			"devices with quota enabled\n", args->name);
	} else {
		do {
			for (i = 0; g_keep_stressing_flag && (i < n_devs); i++) {
				int ret = do_quotas(args, &devs[i]);
				if (ret == EPERM)
					rc = EXIT_SUCCESS;
				if (ret)
					goto tidy;
			}
			inc_counter(args);
		} while (keep_stressing());
	}
	rc = EXIT_SUCCESS;

tidy:
	for (i = 0; i < n_devs; i++)
		free(devs[i].name);

	mount_free(mnts, n_mounts);
	return rc;
}
#else
int stress_quota(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
