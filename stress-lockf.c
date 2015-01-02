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

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#include "stress-ng.h"

#if _BSD_SOURCE || _SVID_SOURCE || _XOPEN_SOURCE >= 500 || \
     (_XOPEN_SOURCE && _XOPEN_SOURCE_EXTENDED)

/*
 *  stress_lockf
 *	stress file locking via lockf()
 */
int stress_lockf(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	int fd, ret = EXIT_FAILURE;
	pid_t ppid = getppid();
	char filename[PATH_MAX];
	char dirname[PATH_MAX];
	char buffer[4096];
	off_t offset;
	const int lock_cmd = (opt_flags & OPT_FLAGS_LOCKF_NONBLK) ?
		F_TLOCK : F_LOCK;


	memset(buffer, 0, sizeof(buffer));

	/*
	 *  There will be a race to create the directory
	 *  so EEXIST is expected on all but one instance
	 */
	(void)stress_temp_dir(dirname, sizeof(dirname), name, ppid, instance);
	if (mkdir(dirname, S_IRWXU) < 0) {
		if (errno != EEXIST) {
			pr_failed_err(name, "mkdir");
			return EXIT_FAILURE;
		}
	}

	/*
	 *  Lock file is based on parent pid and instance 0
	 *  as we need to share this among all the other
	 *  stress flock processes
	 */
	(void)stress_temp_filename(filename, sizeof(filename),
		name, ppid, 0, 0);
retry:
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		if ((errno == ENOENT) && opt_do_run) {
			/* Race, sometimes we need to retry */
			goto retry;
		}
		/* Not sure why this fails.. report and abort */
		pr_failed_err(name, "open");
		(void)rmdir(dirname);
		return EXIT_FAILURE;
	}

	if (lseek(fd, 0, SEEK_SET) < 0) {
		pr_failed_err(name, "lseek");
		goto tidy;
	}
	if (write(fd, buffer, sizeof(buffer)) != sizeof(buffer)) {
		pr_failed_err(name, "write");
		goto tidy;
	}

	do {
		offset = mwc() & 0x1000 ? 0 : sizeof(buffer) / 2;
		if (lseek(fd, offset, SEEK_SET) < 0) {
			pr_failed_err(name, "lseek");
			goto tidy;
		}
		while (lockf(fd, lock_cmd, sizeof(buffer) / 2) < 0) {
			if (!opt_do_run)
				break;
		}
		if (lockf(fd, F_ULOCK, sizeof(buffer) / 2) < 0) {
			pr_failed_err(name, "lockf unlock");
			goto tidy;
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	ret = EXIT_SUCCESS;
tidy:
	(void)close(fd);
	(void)unlink(filename);
	(void)rmdir(dirname);

	return ret;
}

#endif
