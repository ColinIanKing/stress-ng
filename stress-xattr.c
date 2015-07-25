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

#include "stress-ng.h"

#if defined(STRESS_XATTR)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/types.h>
#include <attr/xattr.h>


/*
 *  stress_xattr
 *	stress the xattr operations
 */
int stress_xattr(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	pid_t pid = getpid();
	int fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];

	if (stress_temp_dir_mk(name, pid, instance) < 0)
		return EXIT_FAILURE;

	(void)stress_temp_filename(filename, sizeof(filename),
		name, pid, instance, mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		pr_failed_err(name, "open");
		goto out;
	}
	(void)unlink(filename);

	do {
		int i, j;
		int ret;
		char name[32];
		char value[32];
		ssize_t sz;
		char *buffer;

		for (i = 0; i < 4096; i++) {
			snprintf(name, sizeof(name), "user.var_%d", i);
			snprintf(value, sizeof(value), "orig-value-%i", i);

			ret = fsetxattr(fd, name, value, strlen(value), XATTR_CREATE);
			if (ret < 0) {
				if (errno == ENOTSUP) {
					pr_inf(stderr, "%s stressor will be skipped, filesystem does not support xattr.\n", name);
				}
				if (errno == ENOSPC || errno == EDQUOT)
					break;
				pr_failed_err(name, "fsetxattr");
				goto out_close;
			}
		}
		for (j = 0; j < i; j++) {
			snprintf(name, sizeof(name), "user.var_%d", j);
			snprintf(value, sizeof(value), "value-%i", j);

			ret = fsetxattr(fd, name, value, strlen(value), XATTR_REPLACE);
			if (ret < 0) {
				if (errno == ENOSPC || errno == EDQUOT)
					break;
				pr_failed_err(name, "fsetxattr");
				goto out_close;
			}
		}
		for (j = 0; j < i; j++) {
			char tmp[sizeof(value)];

			snprintf(name, sizeof(name), "user.var_%d", j);
			snprintf(value, sizeof(value), "value-%i", j);

			ret = fgetxattr(fd, name, tmp, sizeof(tmp));
			if (ret < 0) {
				pr_failed_err(name, "fgetxattr");
				goto out_close;
			}
			if (strncmp(value, tmp, ret)) {
				pr_fail(stderr, "%s: fgetxattr values different %.*s vs %.*s\n",
					name, ret, value, ret, tmp);
				goto out_close;
			}
		}
		/* Determine how large a buffer we required... */
		sz = flistxattr(fd, NULL, 0);
		if (sz < 0) {
			pr_failed_err(name, "fremovexattr");
			goto out_close;
		}
		buffer = malloc(sz);
		if (buffer) {
			/* ...and fetch */
			sz = flistxattr(fd, buffer, sz);
			if (sz < 0) {
				pr_failed_err(name, "fremovexattr");
				goto out_close;
			}
			free(buffer);
		}
		for (j = 0; j < i; j++) {
			snprintf(name, sizeof(name), "user.var_%d", j);
			
			ret = fremovexattr(fd, name);
			if (ret < 0) {
				pr_failed_err(name, "fremovexattr");
				goto out_close;
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	rc = EXIT_SUCCESS;
out_close:
	(void)close(fd);
out:
	(void)stress_temp_dir_rm(name, pid, instance);
	return rc;
}

#endif
