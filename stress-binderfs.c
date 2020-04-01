/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
	{ NULL,	"binderfs N",		"start N workers exercising binderfs" },
	{ NULL,	"binderfs-ops N",	"stop after N bogo binderfs operations" },
	{ NULL,	NULL,		    NULL }
};

/*
 *  stress_binderfs_supported()
 *      check if we can run this as root
 */
static int stress_binderfs_supported(void)
{
	if (!stress_check_capability(SHIM_CAP_SYS_ADMIN)) {
		pr_inf("The binderfs stressor will be skipped, "
			"need to be running with CAP_SYS_ADMIN "
			"rights for this stressor\n");
		return -1;
	}
	return 0;
}

#if defined(__linux__) &&			\
    defined(HAVE_LINUX_ANDROID_BINDER_H) &&	\
    defined(HAVE_LINUX_ANDROID_BINDERFS_H)

#define UNMOUNT_TIMEOUT		(5.0)	/* In seconds */

static int stress_binderfs_umount(const stress_args_t *args, const char *pathname)
{
	double t1;

	t1 = stress_time_now();
	for (;;) {
		int ret;
		double t2;

		ret = umount(pathname);
		if (ret == 0)
			break;

		if (errno != EBUSY) {
			pr_err("%s: umount failed on binderfs, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
		t2 = stress_time_now();
		if (t2 - t1 > UNMOUNT_TIMEOUT) {
			pr_err("%s: umount failed, timed out trying after %.3f seconds\n",
				args->name, t2 - t1);
			return EXIT_FAILURE;
		}
		shim_usleep_interruptible(100000);
	}
	return EXIT_SUCCESS;
}

/*
 *  stress_binderfs()
 *      stress binderfs
 */
static int stress_binderfs(const stress_args_t *args)
{
	int rc, ret;
	char pathname[PATH_MAX];
	char filename[PATH_MAX + 16];

	stress_temp_dir(pathname, sizeof(pathname), args->name, args->pid, args->instance);
	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	do {
		int fd;
#if defined(BINDER_CTL_ADD)
		int i;
		struct binderfs_device device;
#endif

		ret = mount("binder", pathname, "binder", 0, 0);
		if (ret < 0) {
			if ((errno == ENODEV) || (errno == ENOSPC) || (errno == ENOMEM)) {
				pr_inf("%s: mount failed on binderfs at %s, errno=%d (%s), skipping stress test\n",
					args->name, pathname, errno, strerror(errno));
				rc = EXIT_NO_RESOURCE;
				goto clean;
			} else {
				pr_err("%s: mount failed on binderfs at %s, errno=%d (%s)\n",
					args->name, pathname, errno, strerror(errno));
				rc = EXIT_FAILURE;
				goto clean;
			}
		}

		(void)snprintf(filename, sizeof(filename), "%s/%s",
			pathname, "binder-control");
		fd = open(filename, O_RDONLY | O_CLOEXEC);
		if (fd < 0) {
			pr_err("%s: cannot open binder control file, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			(void)stress_binderfs_umount(args, pathname);
			rc = EXIT_FAILURE;
			goto clean;
		}
#if defined(BINDER_CTL_ADD)
		for (i = 0; i < 256; i++) {
			(void)memset(&device, 0, sizeof(device));
			(void)snprintf(device.name, sizeof(device.name), "sng-%d\n", i);
			ret = ioctl(fd, BINDER_CTL_ADD, &device);
			if (ret < 0)
				goto close_control;
		}
close_control:
#endif
		(void)close(fd);

		rc = stress_binderfs_umount(args, pathname);
		if (rc != EXIT_SUCCESS)
			break;
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
clean:
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

stressor_info_t stress_binderfs_info = {
	.stressor = stress_binderfs,
	.supported = stress_binderfs_supported,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_binderfs_info = {
	.stressor = stress_not_implemented,
	.supported = stress_binderfs_supported,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
#endif
