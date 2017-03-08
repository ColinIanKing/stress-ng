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
 *  stress_dir_tidy()
 *	remove all dentries
 */
static void stress_dir_tidy(
	const char *rootpath,
	char *path)
{
	for (;;) {
		char *ptr;

		(void)rmdir(path);

		if (!strcmp(rootpath, path))
			break;

		ptr = rindex(path, '/');
		if (*ptr)
			*ptr = '\0';
		else
			break;
	}
}

/*
 *  stress_dir
 *	stress deep recursive directory mkdir and rmdir
 */
int stress_dirdeep(const args_t *args)
{
	int ret = EXIT_SUCCESS;
	size_t rootpathlen;
	char path[PATH_MAX * 4];
	char rootpath[PATH_MAX];

	(void)stress_temp_dir_args(args, rootpath, sizeof(rootpath));
	rootpathlen = strlen(rootpath);

	do {
		size_t len = 0;

		strncpy(path, rootpath, sizeof(path));
		len = rootpathlen;

		for (;;) {
			char tmp[32];

			if (mkdir(path, S_IRUSR | S_IWUSR | S_IXUSR) < 0) {
				char *ptr = rindex(path, '/');
				if (ptr)
					*ptr = '\0';

				if ((errno != ENOSPC) && (errno != ENOMEM) &&
				    (errno != ENAMETOOLONG) && (errno != EDQUOT) &&
				    (errno != EMLINK))
					pr_fail_err("mkdir");
				break;
			}

			if (!keep_stressing())
				goto abort;

			(void)snprintf(tmp, sizeof(tmp), "/%1" PRIu32, mwc32() % 10);
			if (len + 2 >= sizeof(path))
				break;

			strncat(path, tmp, sizeof(path) - len);
			len += 2;

			inc_counter(args);
		}

		stress_dir_tidy(rootpath, path);
		if (!g_keep_stressing_flag)
			break;
		sync();
	} while (keep_stressing());

abort:
	/* force unlink of all files */
	pr_tidy("%s: removing directories\n", args->name);
	stress_dir_tidy(rootpath, path);

	return ret;
}
