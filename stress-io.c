/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
 */
#include "stress-ng.h"
#include "core-mounts.h"

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
static int stress_io(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
#if defined(HAVE_SYNCFS)
	int i, fd, n_mnts;
	char *mnts[MAX_MNTS];
	int fds[MAX_MNTS];
	const int bad_fd = stress_get_bad_fd();

	if (stress_instance_zero(args))
		pr_inf("%s: this is a legacy I/O sync stressor, consider using iomix instead\n", args->name);

	n_mnts = stress_mount_get(mnts, MAX_MNTS);
	for (i = 0; i < n_mnts; i++)
		fds[i] = openat(AT_FDCWD, mnts[i], O_RDONLY | O_NONBLOCK | O_DIRECTORY);

	fd = openat(AT_FDCWD, ".", O_RDONLY | O_NONBLOCK | O_DIRECTORY);
#endif
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		shim_sync();
#if defined(HAVE_SYNCFS)
		if (UNLIKELY((fd != -1) && (syncfs(fd) < 0))) {
			if (UNLIKELY(errno == ENOSYS))
				goto bogo_inc;
			pr_fail("%s: syncfs failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			rc = EXIT_FAILURE;
			goto tidy;
		}

		/* try to sync on all the mount points */
		for (i = 0; i < n_mnts; i++) {
			if (fds[i] < 0)
				continue;
			if (UNLIKELY(syncfs(fds[i]) < 0)) {
				if (UNLIKELY(errno == ENOSYS))
					goto bogo_inc;
				if ((errno != ENOSPC) &&
				    (errno != EDQUOT) &&
				    (errno != EINTR)) {
					pr_fail("%s: syncfs failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					rc = EXIT_FAILURE;
					goto tidy;
				}
			}
		}

		/*
		 *  exercise with an invalid fd
		 */
		if (UNLIKELY(syncfs(bad_fd) == 0)) {
			if (UNLIKELY(errno == ENOSYS))
				goto bogo_inc;
			pr_fail("%s: syncfs on invalid fd %d succeed\n",
				args->name, bad_fd);
			rc = EXIT_FAILURE;
			goto tidy;
		}
bogo_inc:
#else
		UNEXPECTED
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

#if defined(HAVE_SYNCFS)
tidy:
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_SYNCFS)
	if (fd != -1)
		(void)close(fd);

	stress_close_fds(fds, n_mnts);

	stress_mount_free(mnts, n_mnts);
#else
	UNEXPECTED
#endif

	return rc;
}

const stressor_info_t stress_io_info = {
	.stressor = stress_io,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
