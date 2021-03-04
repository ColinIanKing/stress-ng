/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

#define MAX_MNTS	(256)

static const stress_help_t help[] = {
	{ "i N", "io N",	"start N workers spinning on sync()" },
	{ NULL,	 "io-ops N",	"stop sync I/O after N io bogo operations" },
	{ NULL,	 NULL,		NULL }
};

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
static int stress_io(const stress_args_t *args)
{
#if defined(HAVE_SYNCFS)
	int i, fd, n_mnts;
	char *mnts[MAX_MNTS];
	int fds[MAX_MNTS];
	const int bad_fd = stress_get_bad_fd();

	n_mnts = stress_mount_get(mnts, MAX_MNTS);
	for (i = 0; i < n_mnts; i++)
		fds[i] = openat(AT_FDCWD, mnts[i], O_RDONLY | O_NONBLOCK | O_DIRECTORY);

	fd = openat(AT_FDCWD, ".", O_RDONLY | O_NONBLOCK | O_DIRECTORY);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		(void)sync();
#if defined(HAVE_SYNCFS)
		if ((fd != -1) && (syncfs(fd) < 0))
			pr_fail("%s: syncfs failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));

		/* try to sync on all the mount points */
		for (i = 0; i < n_mnts; i++)
			if (fds[i] != -1)
				(void)syncfs(fds[i]);

		/*
		 *  exercise with an invalid fd
		 */
		(void)syncfs(bad_fd);
#endif
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_SYNCFS)
	if (fd != -1)
		(void)close(fd);

	for (i = 0; i < n_mnts; i++)
		if (fds[i] != -1)
			(void)close(fds[i]);

	stress_mount_free(mnts, n_mnts);
#endif

	return EXIT_SUCCESS;
}

stressor_info_t stress_io_info = {
	.stressor = stress_io,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.help = help
};
