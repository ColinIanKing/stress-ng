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

#if defined(HAVE_LINUX_ANDROID_BINDER_H)
#include <linux/android/binder.h>
#endif

#if defined(HAVE_LINUX_ANDROID_BINDERFS_H)
#include <linux/android/binderfs.h>
#endif

#if defined(HAVE_SYS_MOUNT_H)
#include <sys/mount.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"binderfs N",		"start N workers exercising binderfs" },
	{ NULL,	"binderfs-ops N",	"stop after N bogo binderfs operations" },
	{ NULL,	NULL,		    NULL }
};

/*
 *  stress_binderfs_supported()
 *      check if we can run this as root
 */
static int stress_binderfs_supported(const char *name)
{
#if defined(__linux__) &&			\
    defined(HAVE_LINUX_ANDROID_BINDER_H) &&	\
    defined(HAVE_LINUX_ANDROID_BINDERFS_H)
	int ret;
	const char *tmppath = stress_get_temp_path();
	char path[PATH_MAX];

	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf_skip("%s stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n", name);
		return -1;
	}

	if (!tmppath)
		return 0;	/* defer */

	if (stress_temp_dir(path, sizeof(path), "binderfs", getpid(), 0) < 0)
		return 0;	/* defer */
	if (mkdir(path, S_IRWXU) < 0)
		return 0;	/* defer */
	ret = mount("binder", path, "binder", 0, 0);
	if (ret >= 0) {
		(void)umount(path);
		(void)rmdir(path);
		return 0;
	}

	if (errno == ENODEV) {
		pr_inf_skip("%s stressor will be skipped, binderfs not supported\n", name);
	} else {
		pr_inf_skip("%s stressor will be skipped, binderfs cannot be mounted\n", name);
	}
	/* umount just in case it got mounted and mount way lying */
	(void)umount(path);
	(void)rmdir(path);
	return -1;
#else
	pr_inf_skip("%s stressor will be skipped, binderfs not supported\n", name);
	return -1;
#endif
}

#if defined(__linux__) &&			\
    defined(HAVE_LINUX_ANDROID_BINDER_H) &&	\
    defined(HAVE_LINUX_ANDROID_BINDERFS_H)

#define UNMOUNT_TIMEOUT		(15.0)	/* In seconds */

static int stress_binderfs_umount(
	stress_args_t *args,
	const char *pathname,
	double *umount_duration,
	double *umount_count)
{
	double t1;

	t1 = stress_time_now();
	for (;;) {
		double t, t2;
		int ret;

		t = stress_time_now();
		ret = umount(pathname);
		if (ret == 0) {
			(*umount_duration) += stress_time_now() - t;
			(*umount_count) += 1.0;
			break;
		}

		if (errno != EBUSY) {
			pr_fail("%s: umount failed on binderfs, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		t2 = stress_time_now();
		if (t2 - t1 > UNMOUNT_TIMEOUT) {
			pr_fail("%s: umount failed, timed out trying after %.3f seconds\n",
				args->name, t2 - t1);
			return EXIT_FAILURE;
		}
		(void)shim_usleep_interruptible(100000);
	}

	/* Exercise mount on already umounted path */
	VOID_RET(int, umount(pathname));

	/* Exercise mount on invalid path */
	VOID_RET(int, umount(""));

	return EXIT_SUCCESS;
}

/*
 *  stress_binderfs()
 *      stress binderfs
 */
static int stress_binderfs(stress_args_t *args)
{
	int rc, ret;
	char pathname[PATH_MAX];
	char filename[PATH_MAX + 16];
	double mount_duration = 0.0, umount_duration = 0.0;
	double mount_count = 0.0, umount_count = 0.0;
	double rate;

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int fd;
		double t;
#if defined(BINDER_CTL_ADD)
		int i;
		struct binderfs_device device;
#endif

		t = stress_time_now();
		ret = mount("binder", pathname, "binder", 0, 0);
		if (ret >= 0) {
			mount_duration += stress_time_now() - t;
			mount_count += 1.0;
		}
		if (ret < 0) {
			if (errno == ENODEV) {
				/* ENODEV indicates it's not available on this kernel */
				pr_inf_skip("%s: binderfs not supported, errno=%d (%s), skipping stressor\n",
					args->name, errno, strerror(errno));
				rc = EXIT_NO_RESOURCE;
				goto clean;
			} else if ((errno == ENOSPC) ||
				   (errno == ENOMEM) ||
				   (errno == EPERM)) {
				/* ..ran out of resources, skip */
				pr_inf_skip("%s: mount failed on binderfs at %s, errno=%d (%s), skipping stressor\n",
					args->name, pathname, errno, strerror(errno));
				rc = EXIT_NO_RESOURCE;
				goto clean;
			} else {
				/* ..failed! */
				pr_fail("%s: mount failed on binderfs at %s, errno=%d (%s)\n",
					args->name, pathname, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto clean;
			}
		}

		(void)stress_mk_filename(filename, sizeof(filename),
			pathname, "binder-control");
		fd = open(filename, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			pr_fail("%s: cannot open binder control file, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)stress_binderfs_umount(args, pathname, &umount_duration, &umount_count);
			rc = EXIT_FAILURE;
			goto clean;
		}
#if defined(BINDER_CTL_ADD)
		for (i = 0; i < 256; i++) {
			(void)shim_memset(&device, 0, sizeof(device));
			(void)snprintf(device.name, sizeof(device.name), "sng-%d", i);
			ret = ioctl(fd, BINDER_CTL_ADD, &device);
			if (ret < 0)
				goto close_control;
		}
		for (i = 0; i < 256; i++) {
			char devpath[PATH_MAX];
			char devname[32];

			(void)snprintf(devname, sizeof(devname), "sng-%d", i);
			(void)stress_mk_filename(devpath, sizeof(devpath), pathname, devname);
			(void)unlink(devpath);
		}
close_control:
#else
		UNEXPECTED
#endif
		(void)close(fd);

		rc = stress_binderfs_umount(args, pathname, &umount_duration, &umount_count);
		if (rc != EXIT_SUCCESS)
			break;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	rc = EXIT_SUCCESS;
clean:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)stress_temp_dir_rm_args(args);

	rate = (mount_count > 0.0) ? (double)mount_duration / mount_count : 0.0;
	stress_metrics_set(args, 0, "microsecs per mount",
		rate * STRESS_DBL_MICROSECOND, STRESS_METRIC_HARMONIC_MEAN);
	rate = (umount_count > 0.0) ? (double)umount_duration / umount_count : 0.0;
	stress_metrics_set(args, 1, "microsecs per umount",
		rate * STRESS_DBL_MICROSECOND, STRESS_METRIC_HARMONIC_MEAN);

	return rc;
}

const stressor_info_t stress_binderfs_info = {
	.stressor = stress_binderfs,
	.supported = stress_binderfs_supported,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_binderfs_info = {
	.stressor = stress_unimplemented,
	.supported = stress_binderfs_supported,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without android linux/android/binder.h or linux/android/binderfs.h"
};
#endif
