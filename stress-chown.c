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
	{ NULL,	"chown N",	"start N workers thrashing chown file ownership" },
	{ NULL,	"chown-ops N",	"stop chown workers after N bogo operations" },
	{ NULL,	NULL,		NULL }
};

/*
 *  do_fchown()
 *	set ownership different ways
 */
static int do_fchown(
	const int fd,
	const bool cap_chown,
	const uid_t uid,
	const gid_t gid)
{
	int ret, tmp;

	if (fchown(fd, uid, gid) < 0)
		return -errno;
	if (fchown(fd, -1, gid) < 0)
		return -errno;
	if (fchown(fd, uid, -1) < 0)
		return -errno;
	if (fchown(fd, -1, -1) < 0)
		return -errno;

	if (cap_chown)
		return 0;
	if (fchown(fd, 0, 0) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;
	if ((fchown(fd, -1, 0) == 0) && (errno != EPERM))
		goto restore;
	if (errno != EPERM)
		goto restore;
	if (fchown(fd, 0, -1) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;

	return 0;

restore:
	tmp = errno;
	ret = fchown(fd, uid, gid);
	(void)ret;

	return -tmp;
}

/*
 *  do_chown()
 *	set ownership different ways
 */
static int do_chown(
	int (*chown_func)(const char *pathname, uid_t owner, gid_t group),
	const char *filename,
	const bool cap_chown,
	const uid_t uid,
	const gid_t gid)
{
	int ret, tmp;

	if (chown_func(filename, uid, gid) < 0)
		return -errno;
	if (chown_func(filename, -1, gid) < 0)
		return -errno;
	if (chown_func(filename, uid, -1) < 0)
		return -errno;
	if (chown_func(filename, -1, -1) < 0)
		return -errno;

	if (cap_chown)
		return 0;
	if (chown_func(filename, 0, 0) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;
	if ((chown_func(filename, -1, 0) == 0) && (errno != EPERM))
		goto restore;
	if (errno != EPERM)
		goto restore;
	if (chown_func(filename, 0, -1) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;

	return 0;

restore:
	tmp = errno;
	ret = chown_func(filename, uid, gid);
	(void)ret;

	return -tmp;
}

/*
 *  stress_chown
 *	stress chown
 */
static int stress_chown(const args_t *args)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0;
	char filename[PATH_MAX], pathname[PATH_MAX];
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	bool cap_chown = false;

	if (geteuid() == 0)
		cap_chown = true;

	/*
	 *  Allow for multiple workers to chown the *same* file
	 */
	stress_temp_dir(pathname, sizeof(pathname), args->name, ppid, 0);
	if (mkdir(pathname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = exit_status(errno);
			pr_fail_err("mkdir");
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		args->name, ppid, 0, 0);

	if (args->instance == 0) {
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			rc = exit_status(errno);
			pr_fail_err("creat");
			goto tidy;
		}
	} else {
		/* Other instances must try to open the file */
		for (;;) {
			if ((fd = open(filename, O_RDWR, S_IRUSR | S_IWUSR)) > - 1)
				break;

#if defined(__NetBSD__)
			/* For some reason usleep blocks */
			(void)shim_sched_yield();
			retries = 0;
#else
 			(void)shim_usleep(100000);
#endif
			/* Timed out, then give up */
			if (!g_keep_stressing_flag) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			if (++retries >= 100) {
				pr_err("%s: chown: file %s took %d "
					"retries to open and gave up "
					"(instance %" PRIu32 ")\n",
					args->name, filename, retries, args->instance);
				goto tidy;
			}
		}
	}

	do {
		int ret;

		ret = do_fchown(fd, cap_chown, uid, gid);
		if ((ret < 0) && (ret != -EPERM))
			pr_fail_err("fchown");

		ret = do_chown(chown, filename, cap_chown, uid, gid);
		if (ret < 0) {
			if (ret == -ENOENT || ret == -ENOTDIR) {
				/*
				 * File was removed during test by
				 * another worker
				 */
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			if (ret != -EPERM)
				pr_fail_err("chown");
		}
		ret = do_chown(lchown, filename, cap_chown, uid, gid);
		if (ret < 0) {
			if (ret == -ENOENT || ret == -ENOTDIR) {
				/*
				 * File was removed during test by
				 * another worker
				 */
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			if (ret != -EPERM)
				pr_fail_err("chown");
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0)
		(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(pathname);

	return rc;
}

stressor_info_t stress_chown_info = {
	.stressor = stress_chown,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
