/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-filesystem.h"
#include "core-mounts.h"

#define MAX_MNTS	(256)

static const stress_help_t help[] = {
	{ "i N", "io N",	"start N workers spinning on sync()" },
	{ NULL,	 "io-ops N",	"stop sync I/O after N io bogo operations" },
	{ NULL,	 NULL,		NULL }
};

#if defined(HAVE_SYNCFS)
/*
 *  stess_io_write()
 *	write a 32 bit random value to bytes  0..3 of file
 */
static void stess_io_write(const int fd)
{
	uint32_t data = stress_mwc32();

	VOID_RET(off_t, lseek(fd, 0, SEEK_SET));
	VOID_RET(ssize_t, write(fd, &data, sizeof(data)));
}
#endif

/*
 *  stress on sync()
 *	stress system by IO sync calls
 */
static int stress_io(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;
#if defined(HAVE_SYNCFS)
	int ret;
	int i;
	int fd_dir;
	int fd_tmp;
	const int fd_bad = stress_fs_bad_fd_get();
	int n_mnts;
	char *mnts[MAX_MNTS];
	char filename[PATH_MAX];
	int fds[MAX_MNTS];

	if (stress_instance_zero(args))
		pr_inf("%s: this is a legacy I/O sync stressor, consider using iomix instead\n", args->name);

	ret = stress_fs_temp_dir_make_args(args);
	if (ret < 0) {
		rc = stress_exit_status((int)-ret);
		return rc;
	}

	(void)stress_fs_temp_filename_args(args, filename, sizeof(filename), stress_mwc32());
	if ((fd_tmp = open(filename, O_RDWR | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		rc = stress_exit_status(errno);
		pr_fail("%s: open %s failed, errno=%d (%s)\n",
			args->name, filename, errno, strerror(errno));
                goto tidy_dir;

	}
	(void)shim_unlink(filename);

	stress_fs_file_rw_hint_short(fd_tmp);

	n_mnts = stress_mount_get(mnts, MAX_MNTS);
	for (i = 0; i < n_mnts; i++)
		fds[i] = openat(AT_FDCWD, mnts[i], O_RDONLY | O_NONBLOCK | O_DIRECTORY);

	fd_dir = openat(AT_FDCWD, ".", O_RDONLY | O_NONBLOCK | O_DIRECTORY);
#endif
	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_SYNCFS)
		stess_io_write(fd_tmp);
		if (stress_mwc1()) {
			shim_fsync(fd_tmp);
			shim_fdatasync(fd_tmp);
		} else {
			shim_fdatasync(fd_tmp);
			shim_fsync(fd_tmp);
		}

		stess_io_write(fd_tmp);
#endif
		shim_sync();
#if defined(HAVE_SYNCFS)

		stess_io_write(fd_tmp);
		if (UNLIKELY((fd_dir != -1) && (syncfs(fd_dir) < 0))) {
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
		if (UNLIKELY(syncfs(fd_bad) == 0)) {
			pr_fail("%s: syncfs on invalid fd %d succeeded\n",
				args->name, fd_bad);
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
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_SYNCFS)
	if (fd_dir != -1)
		(void)close(fd_dir);

	stress_fs_close_fds(fds, n_mnts);
	stress_mount_free(mnts, n_mnts);
	(void)close(fd_tmp);
#else
	UNEXPECTED
#endif

#if defined(HAVE_SYNCFS)
tidy_dir:
#endif
	(void)stress_fs_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_io_info = {
	.stressor = stress_io,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
