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
	args_t *args,
	const uint64_t n)
{
	uint64_t i;

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		uint64_t gray_code = (i >> 1) ^ i;

		(void)stress_temp_filename(path, sizeof(path),
			args->name, args->pid, args->instance, gray_code);
		(void)unlink(path);
	}
}

/*
 *  stress_mknod
 *	stress mknod creates
 */
int stress_mknod(args_t *args)
{
	const size_t num_nodes = SIZEOF_ARRAY(modes);
	int ret;

	if (num_nodes == 0) {
		pr_err(stderr, "%s: aborting, no valid mknod modes.\n",
			args->name);
		return EXIT_FAILURE;
	}
	ret = stress_temp_dir_mk(args->name, args->pid, args->instance);
	if (ret < 0)
		return exit_status(-ret);

	do {
		uint64_t i, n = DEFAULT_DIRS;

		for (i = 0; i < n; i++) {
			char path[PATH_MAX];
			uint64_t gray_code = (i >> 1) ^ i;
			int mode = modes[mwc32() % num_nodes];

			(void)stress_temp_filename(path, sizeof(path),
				args->name, args->pid, args->instance, gray_code);
			if (mknod(path, mode | S_IRUSR | S_IWUSR, 0) < 0) {
				if ((errno == ENOSPC) || (errno == ENOMEM))
					continue;	/* Try again */
				pr_fail_err(args->name, "mknod");
				n = i;
				break;
			}

			if (!opt_do_run ||
			    (args->max_ops && *args->counter >= args->max_ops))
				goto abort;

			inc_counter(args);
		}
		stress_mknod_tidy(args, n);
		if (!opt_do_run)
			break;
		sync();
	} while (opt_do_run && (!args->max_ops || *args->counter < args->max_ops));

abort:
	/* force unlink of all files */
	pr_tidy(stderr, "%s: removing %" PRIu32 " nodes\n", args->name, DEFAULT_DIRS);
	stress_mknod_tidy(args, DEFAULT_DIRS);
	(void)stress_temp_dir_rm(args->name, args->pid, args->instance);

	return EXIT_SUCCESS;
}
