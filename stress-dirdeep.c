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
int stress_dirdeep(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	int ret = EXIT_SUCCESS;
	size_t rootpathlen;
	char path[PATH_MAX * 4];
	char rootpath[PATH_MAX];

	(void)stress_temp_dir(rootpath, sizeof(rootpath), name, pid, instance);
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
					pr_fail_err(name, "mkdir");
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			snprintf(tmp, sizeof(tmp), "/%1" PRIu32, mwc32() % 10);
			if (len + 2 >= sizeof(path))
				break;

			strncat(path, tmp, sizeof(path) - len);
			len += 2;

			(*counter)++;
		}

		stress_dir_tidy(rootpath, path);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing directories\n", name);
	stress_dir_tidy(rootpath, path);

	return ret;
}
