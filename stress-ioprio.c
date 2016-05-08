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

#if defined(STRESS_IOPRIO)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define MAX_IOV		(4)
#define BUF_SIZE	(32)

/*
 *  stress set/get io priorities
 *	stress system by rapid io priority changes
 */
int stress_ioprio(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	const uid_t uid = getuid();
	const pid_t pgrp = getpgrp();
	int fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return rc;

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	        (void)umask(0077);
	(void)umask(0077);
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err(name, "open");
		goto cleanup_dir;
        }
	(void)unlink(filename);

	do {
		int i;
		struct iovec iov[MAX_IOV];
		char buffer[MAX_IOV][BUF_SIZE];

		if (sys_ioprio_get(IOPRIO_WHO_PROCESS, pid) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PROCESS, %d), "
				"errno = %d (%s)\n",
				name, pid, errno, strerror(errno));
			goto cleanup_file;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PROCESS, 0) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PROCESS, 0), "
				"errno = %d (%s)\n",
				name, errno, strerror(errno));
			goto cleanup_file;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PGRP, pgrp) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PGRP, %d), "
				"errno = %d (%s)\n",
				name, pgrp, errno, strerror(errno));
			goto cleanup_file;
		}
		if (sys_ioprio_get(IOPRIO_WHO_PGRP, 0) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_PGRP, 0), "
				"errno = %d (%s)\n",
				name, errno, strerror(errno));
			goto cleanup_file;
		}
		if (sys_ioprio_get(IOPRIO_WHO_USER, uid) < 0) {
			pr_fail(stderr, "%s: ioprio_get(OPRIO_WHO_USR, %d), "
				"errno = %d (%s)\n",
				name, uid, errno, strerror(errno));
			goto cleanup_file;
		}

		for (i = 0; i < MAX_IOV; i++) {
			memset(buffer[i], mwc8(), BUF_SIZE);
			iov[i].iov_base = buffer[i];
			iov[i].iov_len = BUF_SIZE;
		}

		if (pwritev(fd, iov, MAX_IOV, (off_t)512 * mwc16()) < 0) {
			pr_fail_err(name, "pwritev");
			goto cleanup_file;
		}
		(void)fsync(fd);

		if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
			IOPRIO_PRIO_VALUE(IOPRIO_CLASS_IDLE, 0)) < 0) {
			if (errno != EPERM) {
				pr_fail(stderr, "%s: ioprio_set("
					"IOPRIO_WHO_PROCESS, %d, "
					"(IOPRIO_CLASS_IDLE, 0)), "
					"errno = %d (%s)\n",
					name, pid, errno, strerror(errno));
				goto cleanup_file;
			}
		}

		if (pwritev(fd, iov, MAX_IOV, (off_t)512 * mwc16()) < 0) {
			pr_fail_err(name, "pwritev");
			goto cleanup_file;
		}
		(void)fsync(fd);

		for (i = 0; i < 8; i++) {
			if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, i)) < 0) {
				if (errno != EPERM) {
					pr_fail(stderr, "%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %d, "
						"(IOPRIO_CLASS_BE, %d)), "
						"errno = %d (%s)\n",
						name, pid, i, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			if (pwritev(fd, iov, MAX_IOV, (off_t)512 * mwc16()) < 0) {
				pr_fail_err(name, "pwritev");
				goto cleanup_file;
			}
			(void)fsync(fd);
		}
		for (i = 0; i < 8; i++) {
			if (sys_ioprio_set(IOPRIO_WHO_PROCESS, pid,
				IOPRIO_PRIO_VALUE(IOPRIO_CLASS_RT, i)) < 0) {
				if (errno != EPERM) {
					pr_fail(stderr, "%s: ioprio_set("
						"IOPRIO_WHO_PROCESS, %d, "
						"(IOPRIO_CLASS_RT, %d)), "
						"errno = %d (%s)\n",
						name, pid, i, errno, strerror(errno));
					goto cleanup_file;
				}
			}
			if (pwritev(fd, iov, MAX_IOV, (off_t)512 * mwc16()) < 0) {
				pr_fail_err(name, "pwritev");
				goto cleanup_file;
			}
			(void)fsync(fd);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;

cleanup_file:
	(void)close(fd);
cleanup_dir:
	(void)stress_temp_dir_rm(name, pid, instance);

	return rc;
}

#endif
