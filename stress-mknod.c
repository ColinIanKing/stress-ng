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

static const int modes[] = {
#if defined(S_IFIFO)
	S_IFIFO,	/* FIFO */
#endif
#if defined(S_IFREG)
	S_IFREG,	/* Regular file */
#endif
#if defined(S_IFSOCK)
	S_IFSOCK	/* named socket */
#endif
};

/*
 *  stress_mknod_tidy()
 *	remove all files
 */
static void stress_mknod_tidy(
	const uint64_t n,
	const char *name,
	const pid_t pid,
	const uint64_t instance)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename(path, sizeof(path),
			name, pid, instance, gray_code);
		(void)unlink(path);
	}
}

/*
 *  stress_mknod
 *	stress mknod creates
 */
int stress_mknod(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const pid_t pid = getpid();
	const size_t num_nodes = SIZEOF_ARRAY(modes);
	int ret;

	if (num_nodes == 0) {
		pr_inf(stderr, "%s: aborting, no valid mknod modes.\n",
			name);
		return EXIT_FAILURE;
	}
	ret = stress_temp_dir_mk(name, pid, instance);
	if (ret < 0)
		return exit_status(-ret);

	do {
		uint64_t i, n = DEFAULT_DIRS;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;
			int mode = modes[mwc32() % num_nodes];

			(void)stress_temp_filename(path, sizeof(path),
				name, pid, instance, gray_code);
			if (mknod(path, mode | S_IRUSR | S_IWUSR, 0) < 0) {
				if ((errno == ENOSPC) || (errno == ENOMEM))
					continue;	/* Try again */
				pr_fail_err(name, "mknod");
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (max_ops && *counter >= max_ops))
				goto abort;

			(*counter)++;
		}
		stress_mknod_tidy(n, name, pid, instance);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!max_ops || *counter < max_ops));

abort:
	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing %" PRIu32 " nodes\n", name, DEFAULT_DIRS);
	stress_mknod_tidy(DEFAULT_DIRS, name, pid, instance);
	(void)stress_temp_dir_rm(name, pid, instance);

	return EXIT_SUCCESS;
}
