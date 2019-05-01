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

static const help_t help[] = {
	{ NULL,	"quota N",	"start N workers exercising quotactl commands" },
	{ NULL,	"quota-ops N",	"stop after N quotactl bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYS_QUOTA_H) &&	\
    defined(__linux__) &&		\
    (defined(Q_GETQUOTA) ||		\
     defined(Q_GETFMT) ||		\
     defined(Q_GETINFO) ||		\
     defined(Q_GETSTATS) ||		\
     defined(Q_SYNC))

#define MAX_DEVS	(128)

typedef struct {
	char	*name;
	char	*mount;
	dev_t	st_dev;
	bool	valid;
	bool	enosys;
	bool	esrch;
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
	const char *cmdname,
	int *tested,
	int *failed,
	int *enosys,
	int *esrch,
	int cmd,
	const char *special,
	int id,
	caddr_t addr)
{
	int ret = quotactl(cmd, special, id, addr);

	(*tested)++;
	if (ret < 0) {
		/* Quota not enabled for this file system? */
		if (errno == ESRCH) {
			(*esrch)++;
			return 0;
		}
		/* Not a block device? - ship it */
		if (errno == ENOTBLK)
			return errno;

		if (errno == EPERM) {
			pr_inf("%s: need CAP_SYS_ADMIN capability to "
				"run quota stressor, aborting stress test\n",
				args->name);
			return errno;
		}
		if (errno == ENOSYS) {
			(*enosys)++;
		} else {
			(*failed)++;
			pr_fail("%s: quotactl command %s on %s failed: errno=%d (%s)\n",
				args->name, cmdname, special, errno, strerror(errno));
		}
	}
	return errno;
}

/*
 *  do_quotas()
 *	do quotactl commands
 */
static int do_quotas(const args_t *args, dev_info_t *const dev)
{
	int tested = 0, failed = 0, enosys = 0, esrch = 0;
#if defined(Q_GETQUOTA)
	if (g_keep_stressing_flag) {
		struct dqblk dqblk;
		int err = do_quotactl(args, "Q_GETQUOTA",
				&tested, &failed, &enosys, &esrch,
				QCMD(Q_GETQUOTA, USRQUOTA),
			dev->name, 0, (caddr_t)&dqblk);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETFMT)
	if (g_keep_stressing_flag) {
		uint32_t format;
		int err = do_quotactl(args, "Q_GETFMT",
				&tested, &failed, &enosys, &esrch,
				QCMD(Q_GETFMT, USRQUOTA),
			dev->name, 0, (caddr_t)&format);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETINFO)
	if (g_keep_stressing_flag) {
		struct dqinfo dqinfo;
		int err = do_quotactl(args, "Q_GETINFO",
				&tested, &failed, &enosys, &esrch,
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
		int err = do_quotactl(args, "Q_GETSTATS",
				&tested, &failed, &enosys, &esrch,
				QCMD(Q_GETSTATS, USRQUOTA),
			dev->name, 0, (caddr_t)&dqstats);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_SYNC)
	if (g_keep_stressing_flag) {
		int err = do_quotactl(args, "Q_SYNC",
				&tested, &failed, &enosys, &esrch,
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
	if (!dev->esrch && (esrch > 0)) {
		pr_dbg("%s: quotactl() failed on %s, perhaps not enabled\n",
			args->name, dev->name);
		dev->esrch = true;
	}
	if (tested == enosys) {
		pr_dbg("%s: quotactl() failed on %s, not available "
			"on this kernel or filesystem\n", args->name, dev->name);
		dev->enosys = true;
		return ENOSYS;
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
static int stress_quota(const args_t *args)
{
	int i, n_mounts, n_devs = 0;
	int rc = EXIT_FAILURE;
	char *mnts[MAX_DEVS];
	dev_info_t devs[MAX_DEVS];
	DIR *dir;
	struct dirent *d;
	struct stat buf;

	(void)memset(mnts, 0, sizeof(mnts));
	(void)memset(devs, 0, sizeof(devs));

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
					(void)closedir(dir);
					goto tidy;
				}
			}
		}
	}
	(void)closedir(dir);

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
		(void)memset(&devs[i], 0, sizeof(devs[i]));

	if (!n_devs) {
		pr_err("%s: cannot find any candidate block "
			"devices with quota enabled\n", args->name);
	} else {
		do {
			int failed = 0, enosys = 0;

			for (i = 0; g_keep_stressing_flag && (i < n_devs); i++) {
				int ret;

				/* This failed before, so don't retest */
				if (devs[i].enosys) {
					enosys++;
					continue;
				}

				ret = do_quotas(args, &devs[i]);
				switch (ret) {
				case 0:
					break;
				case ENOSYS:
					enosys++;
					break;
				case EPERM:
					goto abort;
					break;
				default:
					failed++;
					break;
				}
			}
			inc_counter(args);

			/*
			 * Accounting not on for all the devices?
			 * then do a non-fatal skip test
			 */
			if (enosys == n_devs) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			
			/* All failed, then give up */
			if (failed == n_devs)
				goto tidy;
		} while (keep_stressing());
	}
abort:
	rc = EXIT_SUCCESS;

tidy:
	for (i = 0; i < n_devs; i++)
		free(devs[i].name);

	mount_free(mnts, n_mounts);
	return rc;
}

stressor_info_t stress_quota_info = {
	.stressor = stress_quota,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_quota_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
