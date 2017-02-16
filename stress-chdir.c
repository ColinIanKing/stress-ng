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

/*
 *  stress_chdir
 *	stress chdir calls
 */
int stress_chdir(const args_t *args)
{
	uint64_t i;
	char path[PATH_MAX], cwd[PATH_MAX];
	int rc, ret = EXIT_FAILURE;
	char *paths[DEFAULT_DIRS];

	memset(paths, 0, sizeof(paths));

	if (getcwd(cwd, sizeof(cwd)) == NULL) {
		pr_fail_err("getcwd");
		return ret;
	}

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return exit_status(-rc);

	/* Populate */
	for (i = 0; i < DEFAULT_DIRS; i++) {
		uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename_args(args,
			path, sizeof(path), gray_code);
		paths[i] = strdup(path);
		if (paths[i] == NULL)
			goto abort;
		rc = mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR);
		if (rc < 0) {
			ret = exit_status(errno);
			pr_fail_err("mkdir");
			goto abort;
		}
		if (!g_keep_stressing_flag)
			goto done;
	}

	do {
		for (i = 0; i < DEFAULT_DIRS; i++) {
			if (!keep_stressing())
				goto done;
			if (chdir(paths[i]) < 0) {
				if (errno != ENOMEM) {
					pr_fail_err("chdir");
					goto abort;
				}
			}
redo:
			if (!keep_stressing())
				goto done;
			/* We need chdir to cwd to always succeed */
			if (chdir(cwd) < 0) {
				/* Maybe low memory, force retry */
				if (errno == ENOMEM)
					goto redo;
				pr_fail_err("chdir");
				goto abort;
			}
		}
		inc_counter(args);
	} while (keep_stressing());
done:
	ret = EXIT_SUCCESS;
abort:
	if (chdir(cwd) < 0)
		pr_fail_err("chdir");

	/* force unlink of all files */
	pr_tidy("%s: removing %" PRIu32 " directories\n",
		args->name, DEFAULT_DIRS);

	for (i = 0; (i < DEFAULT_DIRS) && paths[i] ; i++) {
		(void)rmdir(paths[i]);
		free(paths[i]);
	}
	(void)stress_temp_dir_rm_args(args);

	return ret;
}
