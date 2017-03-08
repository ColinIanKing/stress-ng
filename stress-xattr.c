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

#if defined(__linux__) && defined(HAVE_XATTR_H)

#include <attr/xattr.h>

/*
 *  stress_xattr
 *	stress the xattr operations
 */
int stress_xattr(const args_t *args)
{
	int ret, fd, rc = EXIT_FAILURE;
	char filename[PATH_MAX];

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args, filename, sizeof(filename), mwc32());
	(void)umask(0077);
	if ((fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR)) < 0) {
		rc = exit_status(errno);
		pr_fail_err("open");
		goto out;
	}
	(void)unlink(filename);

	do {
		int i, j;
		char attrname[32];
		char value[32];
		ssize_t sz;
		char *buffer;

		for (i = 0; i < 4096; i++) {
			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", i);
			(void)snprintf(value, sizeof(value), "orig-value-%d", i);

			ret = fsetxattr(fd, attrname, value, strlen(value), XATTR_CREATE);
			if (ret < 0) {
				if (errno == ENOTSUP) {
					pr_inf("%s stressor will be "
						"skipped, filesystem does not "
						"support xattr.\n", args->name);
				}
				if (errno == ENOSPC || errno == EDQUOT)
					break;
				pr_fail_err("fsetxattr");
				goto out_close;
			}
		}
		for (j = 0; j < i; j++) {
			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);
			(void)snprintf(value, sizeof(value), "value-%d", j);

			ret = fsetxattr(fd, attrname, value, strlen(value),
				XATTR_REPLACE);
			if (ret < 0) {
				if (errno == ENOSPC || errno == EDQUOT)
					break;
				pr_fail_err("fsetxattr");
				goto out_close;
			}
		}
		for (j = 0; j < i; j++) {
			char tmp[sizeof(value)];

			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);
			(void)snprintf(value, sizeof(value), "value-%d", j);

			ret = fgetxattr(fd, attrname, tmp, sizeof(tmp));
			if (ret < 0) {
				pr_fail_err("fgetxattr");
				goto out_close;
			}
			if (strncmp(value, tmp, ret)) {
				pr_fail("%s: fgetxattr values "
					"different %.*s vs %.*s\n",
					args->name, ret, value, ret, tmp);
				goto out_close;
			}
		}
		/* Determine how large a buffer we required... */
		sz = flistxattr(fd, NULL, 0);
		if (sz < 0) {
			pr_fail_err("flistxattr");
			goto out_close;
		}
		buffer = malloc(sz);
		if (buffer) {
			/* ...and fetch */
			sz = flistxattr(fd, buffer, sz);
			free(buffer);

			if (sz < 0) {
				pr_fail_err("flistxattr");
				goto out_close;
			}
		}
		for (j = 0; j < i; j++) {
			(void)snprintf(attrname, sizeof(attrname), "user.var_%d", j);

			ret = fremovexattr(fd, attrname);
			if (ret < 0) {
				pr_fail_err("fremovexattr");
				goto out_close;
			}
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
out_close:
	(void)close(fd);
out:
	(void)stress_temp_dir_rm_args(args);
	return rc;
}
#else
int stress_xattr(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
