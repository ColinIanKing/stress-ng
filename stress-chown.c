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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>

#include "stress-ng.h"

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
	if (fchown(fd, -1, 0) == 0)
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
	const char *filename,
	const bool cap_chown,
	const uid_t uid,
	const gid_t gid)
{
	int ret, tmp;

	if (chown(filename, uid, gid) < 0)
		return -errno;
	if (chown(filename, -1, gid) < 0)
		return -errno;
	if (chown(filename, uid, -1) < 0)
		return -errno;
	if (chown(filename, -1, -1) < 0)
		return -errno;

	if (cap_chown)
		return 0;
	if (chown(filename, 0, 0) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;
	if (chown(filename, -1, 0) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;
	if (chown(filename, 0, -1) == 0)
		goto restore;
	if (errno != EPERM)
		goto restore;

	return 0;

restore:
	tmp = errno;
	ret = chown(filename, uid, gid);
	(void)ret;

	return -tmp;
}

/*
 *  stress_chown
 *	stress chown
 */
int stress_chown(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t ppid = getppid();
	int fd = -1, rc = EXIT_FAILURE, retries = 0;
	char filename[PATH_MAX], dirname[PATH_MAX];
	const uid_t uid = getuid();
	const gid_t gid = getgid();
	bool cap_chown = false;

	if (geteuid() == 0)
		cap_chown = true;

	/*
	 *  Allow for multiple workers to chown the *same* file
	 */
	stress_temp_dir(dirname, sizeof(dirname), name, ppid, 0);
        if (mkdir(dirname, S_IRUSR | S_IRWXU) < 0) {
		if (errno != EEXIST) {
			rc = exit_status(errno);
			pr_fail_err(name, "mkdir");
			return rc;
		}
	}
	(void)stress_temp_filename(filename, sizeof(filename),
		name, ppid, 0, 0);

	if (instance == 0) {
		if ((fd = creat(filename, S_IRUSR | S_IWUSR)) < 0) {
			rc = exit_status(errno);
			pr_fail_err(name, "creat");
			goto tidy;
		}
	} else {
		/* Other instances must try to open the file */
		for (;;) {
			if ((fd = open(filename, O_RDWR, S_IRUSR | S_IWUSR)) > - 1)
				break;

			(void)usleep(100000);
			if (++retries >= 100) {
				pr_err(stderr, "%s: chown: file %s took %d retries to open and gave up(instance %" PRIu32 ")\n",
					name, filename, retries, instance);
				goto tidy;
			}
			/* Timed out, then give up */
			if (!opt_do_run) {
				rc = EXIT_SUCCESS;
				goto tidy;
			}
		}
	}

	do {
		int ret;

		ret = do_fchown(fd, cap_chown, uid, gid);
		if (ret < 0) {
			pr_fail(stderr, "%s: fchown: errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		ret = do_chown(filename, cap_chown, uid, gid);
		if (ret < 0) {
			if (ret == -ENOENT || errno == -ENOTDIR) {
				/*
				 * File was removed during test by
				 * another worker
				 */
				rc = EXIT_SUCCESS;
				goto tidy;
			}
			pr_fail(stderr, "%s: chown: errno=%d (%s)\n",
				name, errno, strerror(errno));
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
tidy:
	if (fd >= 0)
		(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return rc;
}
