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
#include "core-capabilities.h"
#include "core-mounts.h"

#if defined(HAVE_SYS_QUOTA_H)
#include <sys/quota.h>
#endif

static const stress_help_t help[] = {
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
	char	*name;		/* Device name */
	char	*mount;		/* Mount point */
	dev_t	st_dev;		/* Device major/minor */
	bool	valid;		/* A valid device that is mountable */
	bool	skip;		/* Skip testing this device */
} stress_dev_info_t;

struct shim_nextdqblk {
	uint64_t dqb_bhardlimit;
	uint64_t dqb_bsoftlimit;
	uint64_t dqb_curspace;
	uint64_t dqb_ihardlimit;
	uint64_t dqb_isoftlimit;
	uint64_t dqb_curinodes;
	uint64_t dqb_btime;
	uint64_t dqb_itime;
	uint32_t dqb_valid;
	uint32_t dqb_id;
};

/* Account different failure modes */
typedef struct {
	int tested;	/* Device test count */
	int failed;	/* Device test failure count */
	int enosys;	/* No system call */
	int esrch;	/* Device path not found */
	int erofs;	/* Read only device */
	int enotblk;	/* Not a block device */
} quotactl_status_t;

/*
 *  stress_quota_supported()
 *      check if we can run this with SHIM_CAP_SYS_ADMIN capability
 */
static int stress_quota_supported(const char *name)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}
	return 0;
}

/*
 *  do_quotactl_call()
 *	try to do quotactl or quotactl_path calls, randomly
 *	selected either. If quotactl_path does not exist then
 *	just fall back to quotactl.
 */
static int do_quotactl_call(
	const int cmd,
	const stress_dev_info_t *dev,
	const int id,
	const caddr_t addr)
{
	static bool have_quotactl_fd = true;
	int ret, fd, saved_errno;

	/*
	 *  quotactl_fd() failed on ENOSYS or random choice
	 *  then do normal quotactl call
	 */
	if (!have_quotactl_fd || stress_mwc1())
		goto do_quotactl;

	/*
	 *  try quotactl_fd() instead, it may not exist
	 *  so flag this for next time and do normal quotactl
	 *  call
	 */
	fd = open(dev->mount, O_DIRECTORY | O_RDONLY);
	if (fd < 0)
		goto do_quotactl;
	ret = shim_quotactl_fd((unsigned int)fd, (unsigned int)cmd, id, addr);
	if ((ret < 0) && (errno == ENOSYS)) {
		/* We don't have quotactl_path, use quotactl */
		have_quotactl_fd = false;
		(void)close(fd);
		goto do_quotactl;
	}
	saved_errno = errno;
	(void)close(fd);
	errno = saved_errno;
	return ret;

do_quotactl:
	return quotactl(cmd, dev->name, id, addr);
}


/*
 *  do_quotactl()
 *	do a quotactl command
 */
static int do_quotactl(
	stress_args_t *args,
	const char *cmdname,
	quotactl_status_t *status,
	const int cmd,
	stress_dev_info_t *dev,
	const int id,
	const caddr_t addr)
{
	int ret = do_quotactl_call(cmd, dev, id, addr);

	status->tested++;
	if (ret < 0) {
		/* Quota not enabled for this file system? */
		switch (errno) {
		case ENOSYS:
			status->enosys++;
			break;
		case ESRCH:
			status->esrch++;
			return 0;
		case EROFS:
			/* Read-only device?- skip it */
			status->erofs++;
			return errno;
		case ENOTBLK:
			status->enotblk++;
			/* Not a block device? - skip it */
			return errno;
		case EPERM:
			pr_inf("%s: need CAP_SYS_ADMIN capability to "
				"run quota stressor, aborting stress test\n",
				args->name);
			return errno;
		default:
			status->failed++;
			pr_fail("%s: quotactl command %s on %s (%s) failed, errno=%d (%s)\n",
				args->name, cmdname, dev->name, dev->mount, errno, strerror(errno));
		}
	}
	return errno;
}

/*
 *  do_quotas()
 *	do quotactl commands
 */
static int do_quotas(stress_args_t *args, stress_dev_info_t *const dev)
{
	int err;
	char buffer[1024];
	quotactl_status_t status;

	(void)shim_memset(&status, 0, sizeof(status));

#if defined(Q_GETQUOTA)
	if (LIKELY(stress_continue_flag())) {
		struct dqblk dqblk;

		(void)shim_memset(&dqblk, 0, sizeof(dqblk));
		err = do_quotactl(args, "Q_GETQUOTA", &status,
			QCMD(Q_GETQUOTA, USRQUOTA),
			dev, 0, (caddr_t)&dqblk);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETNEXTQUOTA)
	if (LIKELY(stress_continue_flag())) {
		struct shim_nextdqblk nextdqblk;

		(void)shim_memset(&nextdqblk, 0, sizeof(nextdqblk));
		err = do_quotactl(args, "Q_GETNEXTQUOTA", &status,
			QCMD(Q_GETNEXTQUOTA, USRQUOTA),
			dev, 0, (caddr_t)&nextdqblk);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETFMT)
	if (LIKELY(stress_continue_flag())) {
		uint32_t format;

		(void)shim_memset(&format, 0, sizeof(format));
		err = do_quotactl(args, "Q_GETFMT", &status,
			QCMD(Q_GETFMT, USRQUOTA),
			dev, 0, (caddr_t)&format);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETINFO)
	if (LIKELY(stress_continue_flag())) {
		struct dqinfo dqinfo;

		(void)shim_memset(&dqinfo, 0, sizeof(dqinfo));
		err = do_quotactl(args, "Q_GETINFO", &status,
			QCMD(Q_GETINFO, USRQUOTA),
			dev, 0, (caddr_t)&dqinfo);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_GETSTATS)
	/* Obsolete in recent kernels */
	if (LIKELY(stress_continue_flag())) {
		struct dqstats dqstats;

		(void)shim_memset(&dqstats, 0, sizeof(dqstats));
		err = do_quotactl(args, "Q_GETSTATS", &status,
			QCMD(Q_GETSTATS, USRQUOTA),
			dev, 0, (caddr_t)&dqstats);
		if (err == EPERM)
			return err;
	}
#endif
#if defined(Q_SYNC)
	if (LIKELY(stress_continue_flag())) {
		err = do_quotactl(args, "Q_SYNC", &status,
			QCMD(Q_SYNC, USRQUOTA),
			dev, 0, 0);
		if (err == EPERM)
			return err;
	}
#endif
	/*
	 *  ..and exercise with some invalid arguments..
	 */
	VOID_RET(int, quotactl(~0, dev->name, USRQUOTA, (caddr_t)buffer));
#if defined(Q_GETINFO)
	{
		struct dqinfo dqinfo;

		(void)shim_memset(&dqinfo, 0, sizeof(dqinfo));
		VOID_RET(int, quotactl(QCMD(Q_GETQUOTA, USRQUOTA), "", 0, (caddr_t)&dqinfo));
		VOID_RET(int, quotactl(QCMD(Q_GETQUOTA, USRQUOTA), dev->name, ~0, (caddr_t)&dqinfo));
		VOID_RET(int, quotactl(QCMD(Q_GETQUOTA, -1), dev->name, ~0, (caddr_t)&dqinfo));
	}
#endif
#if defined(Q_SYNC)
	/* special Q_SYNC without specific device will sync all */
	VOID_RET(int, quotactl(QCMD(Q_SYNC, USRQUOTA), NULL, 0, NULL));

	/* invalid Q_SYNC with "" device name */
	VOID_RET(int, quotactl(QCMD(Q_SYNC, USRQUOTA), "", 0, NULL));
#endif

	if (status.tested == 0) {
		pr_err("%s: quotactl() failed, quota commands "
			"not available\n", args->name);
		return -1;
	}
	if (!dev->skip && (status.esrch > 0)) {
		pr_dbg("%s: quotactl() failed on %s, perhaps not enabled\n",
			args->name, dev->name);
		dev->skip = true;
	}
	if (status.tested == status.enosys) {
		pr_dbg("%s: quotactl() failed on %s, not available "
			"on this kernel or filesystem\n", args->name, dev->name);
		dev->skip = true;
		return ENOSYS;
	}
	if (status.tested == status.enotblk) {
		pr_dbg("%s: quotactl() failed on %s, device is not a block device\n",
			args->name, dev->name);
		dev->skip = true;
		return ENOTBLK;
	}
	if (status.tested == status.erofs) {
		pr_dbg("%s: quotactl() failed on %s, device is a read-only device\n",
			args->name, dev->name);
		dev->skip = true;
		return EROFS;
	}
	if (status.tested == status.failed) {
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
static int stress_quota(stress_args_t *args)
{
	int i, n_mounts, n_devs = 0;
	int rc = EXIT_FAILURE;
	char *mnts[MAX_DEVS];
	stress_dev_info_t devs[MAX_DEVS];
	DIR *dir;
	const struct dirent *d;
	struct stat buf;

	(void)shim_memset(mnts, 0, sizeof(mnts));
	(void)shim_memset(devs, 0, sizeof(devs));

	n_mounts = stress_mount_get(mnts, SIZEOF_ARRAY(mnts));

	dir = opendir("/dev/");
	if (!dir) {
		pr_err("%s: opendir on /dev failed, errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return rc;
	}

	for (i = 0; i < n_mounts; i++) {
		if (shim_lstat(mnts[i], &buf) == 0) {
			devs[i].st_dev = buf.st_dev;
			devs[i].valid = true;
		}
	}

	while ((d = readdir(dir)) != NULL) {
		char path[PATH_MAX];

		(void)snprintf(path, sizeof(path), "/dev/%s", d->d_name);
		if (shim_lstat(path, &buf) < 0)
			continue;
		if ((buf.st_mode & S_IFBLK) == 0)
			continue;
		for (i = 0; i < n_mounts; i++) {
			if (devs[i].valid &&
			    !devs[i].name &&
			    (buf.st_rdev == devs[i].st_dev)) {
				devs[i].name = shim_strdup(path);
				devs[i].mount = mnts[i];
				if (!devs[i].name) {
					pr_err("%s: out of memory\n",
						args->name);
					(void)closedir(dir);
					n_devs = i;
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
		if (devs[i].name) {
			if (unique) {
				devs[n_devs++] = devs[i];
			} else {
				free(devs[i].name);
				devs[i].name = NULL;
				devs[i].valid = false;
			}
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (!n_devs) {
		pr_err("%s: cannot find any candidate block "
			"devices with quota enabled\n", args->name);
	} else {
		do {
			int failed = 0, skipped = 0;

			for (i = 0; LIKELY(stress_continue_flag() && (i < n_devs)); i++) {
				int ret;

				/* This failed before, so don't re-test */
				if (devs[i].skip) {
					skipped++;
					continue;
				}

				ret = do_quotas(args, &devs[i]);
				switch (ret) {
				case 0:
					break;
				case ENOSYS:
				case EROFS:
				case ENOTBLK:
					skipped++;
					break;
				case EPERM:
					goto abort;
				default:
					failed++;
					break;
				}
			}
			stress_bogo_inc(args);

			/*
			 * Accounting not on for all the devices?
			 * then do a non-fatal skip test
			 */
			if (skipped == n_devs) {
				pr_inf("%s: cannot test accounting on available devices, "
					"skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				goto tidy;
			}

			/* All failed, then give up */
			if (failed == n_devs)
				goto tidy;
		} while (stress_continue(args));
	}
abort:
	rc = EXIT_SUCCESS;

tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0; i < n_devs; i++)
		free(devs[i].name);

	stress_mount_free(mnts, n_mounts);
	return rc;
}

const stressor_info_t stress_quota_info = {
	.stressor = stress_quota,
	.supported = stress_quota_supported,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_quota_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sys/quota.h or only supported on Linux"
};
#endif
