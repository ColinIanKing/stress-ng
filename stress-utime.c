/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"utime N",	"start N workers updating file timestamps" },
	{ NULL,	"utime-ops N",	"stop after N utime bogo operations" },
	{ NULL,	"utime-fsync",	"force utime meta data sync to the file system" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_utime_fsync(const char *opt)
{
	bool utime_fsync = true;

	(void)opt;
	return set_setting("utime-fsync", TYPE_ID_BOOL, &utime_fsync);
}

/*
 *  stress_utime()
 *	stress system by setting file utime
 */
static int stress_utime(const args_t *args)
{
	char filename[PATH_MAX];
	int ret, fd;
	bool utime_fsync = false;

	(void)get_setting("utime-fsync", &utime_fsync);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_err("%s: open failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		return ret;
	}

	do {
		struct timeval timevals[2];
#if (defined(HAVE_FUTIMENS) || defined(HAVE_UTIMENSAT)) && \
    (defined(UTIME_NOW) || defined(UTIME_OMIT))
		struct timespec ts[2];
#endif

		if (gettimeofday(&timevals[0], NULL) == 0) {
			timevals[1] = timevals[0];
			if (utimes(filename, timevals) < 0) {
				pr_dbg("%s: utimes failed: errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
		if (utimes(filename, NULL) < 0) {
			pr_dbg("%s: utimes failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#if defined(HAVE_FUTIMENS)
		if (futimens(fd, NULL) < 0) {
			pr_dbg("%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

#if defined(UTIME_NOW)
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;
		if (futimens(fd, &ts[0]) < 0) {
			pr_dbg("%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#endif

#if defined(UTIME_OMIT)
		ts[0].tv_sec = UTIME_OMIT;
		ts[0].tv_nsec = UTIME_OMIT;
		if (futimens(fd, &ts[0]) < 0) {
			pr_dbg("%s: futimens failed: errno=%d: (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#endif
#endif

#if defined(HAVE_UTIMENSAT)

#if defined(UTIME_NOW)
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;

		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;

		(void)utimensat(AT_FDCWD, filename, ts, 0);
#endif

#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		(void)utimensat(AT_FDCWD, filename, ts, 0);
#endif

#if defined(AT_SYMLINK_NOFOLLOW)
#if defined(UTIME_NOW)
		ts[1].tv_nsec = UTIME_NOW;
		(void)utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW);
#endif
#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		(void)utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW);
#endif
#endif
		if (utime_fsync)
			(void)shim_fsync(fd);
#endif

#if defined(HAVE_UTIME_H)
		{
			struct utimbuf utbuf;

			utbuf.actime = (time_t)time_now();
			utbuf.modtime = utbuf.actime;

			if (utime(filename, &utbuf) < 0) {
				pr_dbg("%s: utime failed: errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
			if (utime(filename, NULL) < 0) {
				pr_dbg("%s: utime failed: errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}
		}
#endif
		/* forces metadata writeback */
		if (utime_fsync)
			(void)shim_fsync(fd);
		inc_counter(args);
	} while (keep_stressing());

	(void)close(fd);
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
        { OPT_utime_fsync,	stress_set_utime_fsync },
	{ 0,			NULL }
};

stressor_info_t stress_utime_info = {
	.stressor = stress_utime,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
