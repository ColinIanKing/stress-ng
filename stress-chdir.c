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
#include <inttypes.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "stress-ng.h"

/*
 *  stress_chdir
 *	stress chdir calls
 */
int stress_chdir(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	uint64_t i;
	char path[PATH_MAX], cwd[PATH_MAX];
	int rc, ret = EXIT_FAILURE;
	char *paths[DEFAULT_DIRS];

	memset(paths, 0, sizeof(paths));

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		pr_fail_err(name, "getcwd");
		return ret;
	}

	rc = stress_temp_dir_mk(name, pid, instance);
	if (rc < 0)
		return exit_status(-rc);

	/* Populate */
	for (i = 0; i < DEFAULT_DIRS; i++) {
		uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename(path, sizeof(path),
			name, pid, instance, gray_code);
		paths[i] = strdup(path);
		if (paths[i] == NULL)
			goto abort;
		rc = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
		if (rc < 0) {
			ret = exit_status(errno);
			pr_fail_err(name, "mkdir");
			goto abort;
		}
		if (!opt_do_run)
			goto done;
	}

	do {
		for (i = 0; i < DEFAULT_DIRS; i++) {
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				goto done;
			if (chdir(paths[i]) < 0) {
				if (errno != ENOMEM) {
					pr_fail_err(name, "chdir");
					goto abort;
				}
			}
redo:
			if (!opt_do_run || (max_ops && *counter >= max_ops))
				goto done;
			/* We need chdir to cwd to always succeed */
			if (chdir(cwd) < 0) {
				if (errno == ENOMEM)	/* Maybe low memory, force retry */
					goto redo;
				pr_fail_err(name, "chdir");
				goto abort;
			}
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));
done:
	ret = EXIT_SUCCESS;
abort:
	if (chdir(cwd) < 0)
		pr_fail_err(name, "chdir");

	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing %" PRIu32 " directories\n", name, DEFAULT_DIRS);

	for (i = 0; (i < DEFAULT_DIRS) && paths[i] ; i++) {
		(void)rmdir(paths[i]);
		free(paths[i]);
	}
	(void)stress_temp_dir_rm(name, pid, instance);

	return ret;
}
