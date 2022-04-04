/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-pragma.h"

#if defined(HAVE_UTIME_H)
#include <utime.h>
#else
UNEXPECTED
#endif

static const stress_help_t help[] = {
	{ NULL,	"utime N",	"start N workers updating file timestamps" },
	{ NULL,	"utime-ops N",	"stop after N utime bogo operations" },
	{ NULL,	"utime-fsync",	"force utime meta data sync to the file system" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_utime_fsync(const char *opt)
{
	bool utime_fsync = true;

	(void)opt;
	return stress_set_setting("utime-fsync", TYPE_ID_BOOL, &utime_fsync);
}

static int shim_utime(const char *filename, const struct utimbuf *times)
{
#if defined(__NR_utime)
	return (int)syscall(__NR_utime, filename, times);
#else
	return utime(filename, times)
#endif
}

/*
 *  stress_utime()
 *	stress system by setting file utime
 */
static int stress_utime(const stress_args_t *args)
{
#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW)
	char path[PATH_MAX];
#endif
	int dir_fd = -1;
	char filename[PATH_MAX];
	char hugename[PATH_MAX + 16];
	int ret, fd;
	bool utime_fsync = false;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	(void)stress_get_setting("utime-fsync", &utime_fsync);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return exit_status(-ret);

#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW)
	(void)stress_temp_dir_args(args, path, sizeof(path));
	dir_fd = open(path, O_DIRECTORY | O_PATH);
#else
	UNEXPECTED
#endif

	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());
	if ((fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR)) < 0) {
		ret = exit_status(errno);
		pr_err("%s: open failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		if (dir_fd >= 0) /* cppcheck-suppress knownConditionTrueFalse */
			(void)close(dir_fd);
		return ret;
	}

	stress_strnrnd(hugename, sizeof(hugename));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		struct timeval timevals[2];
#if (defined(HAVE_FUTIMENS) || defined(HAVE_UTIMENSAT)) && \
    (defined(UTIME_NOW) || defined(UTIME_OMIT))
		struct timespec ts[2];
#endif

		(void)gettimeofday(&timevals[0], NULL);
		timevals[1] = timevals[0];
		if (utimes(filename, timevals) < 0) {
			pr_dbg("%s: utimes failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		if (utimes(filename, NULL) < 0) {
			pr_dbg("%s: utimes failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		/* Exercise with invalid filename, ENOENT */
		ret = utimes("", timevals);
		(void)ret;

		/* Exercise huge filename, ENAMETOOLONG */
		ret = utimes(hugename, timevals);
		(void)ret;

#if defined(HAVE_FUTIMENS)
		if (futimens(fd, NULL) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}

		/* Exercise with invalid fd */
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;
		ret = futimens(-1, ts);
		(void)ret;

#if defined(UTIME_NOW)
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;
		if (futimens(fd, ts) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#else
		UNEXPECTED
#endif

#if defined(UTIME_OMIT)
		ts[0].tv_sec = UTIME_OMIT;
		ts[0].tv_nsec = UTIME_OMIT;
		if (futimens(fd, ts) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
#else
		UNEXPECTED
#endif
#endif

#if defined(HAVE_UTIMENSAT)

#if defined(UTIME_NOW)
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;

		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;

		ret = utimensat(AT_FDCWD, filename, ts, 0);
		(void)ret;

		/* Exercise invalid filename, ENOENT */
		ret = utimensat(AT_FDCWD, "", ts, 0);
		(void)ret;

		/* Exercise huge filename, ENAMETOOLONG */
		ret = utimensat(AT_FDCWD, hugename, ts, 0);
		(void)ret;

		/* Exercise invalid flags */
		ret = utimensat(AT_FDCWD, filename, ts, ~0);
		(void)ret;
#else
		UNEXPECTED
#endif

#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW) &&	\
    defined(HAVE_PRAGMA_INSIDE)
STRESS_PRAGMA_PUSH
STRESS_PRAGMA_WARN_OFF
		if (dir_fd >= 0) {
			ts[0].tv_sec = UTIME_NOW;
			ts[0].tv_nsec = UTIME_NOW;

			ts[1].tv_sec = UTIME_NOW;
			ts[1].tv_nsec = UTIME_NOW;

			ret = utimensat(dir_fd, "", ts, 0);
			(void)ret;
		}
STRESS_PRAGMA_POP
#endif

#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		ret = utimensat(AT_FDCWD, filename, ts, 0);
		(void)ret;
#endif


#if defined(AT_SYMLINK_NOFOLLOW)
#if defined(UTIME_NOW)
		ts[1].tv_nsec = UTIME_NOW;
		ret = utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW);
		(void)ret;
		if (utime_fsync) {
			ret = shim_fsync(fd);
			(void)ret;
		}
#endif
#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		ret = utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW);
		(void)ret;
		if (utime_fsync) {
			ret = shim_fsync(fd);
			(void)ret;
		}
#endif
#endif
#else
		UNEXPECTED
#endif

#if defined(HAVE_UTIME_H)
		{
			struct utimbuf utbuf;
			struct timeval tv;

			(void)gettimeofday(&tv, NULL);
			utbuf.actime = (time_t)tv.tv_sec;
			utbuf.modtime = utbuf.actime;

			if (shim_utime(filename, &utbuf) < 0) {
				pr_fail("%s: utime failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}

			if (verify) {
				struct stat statbuf;

				if (stat(filename, &statbuf) == 0) {
					if (statbuf.st_atime < tv.tv_sec) {
						pr_fail("%s: utime failed: access time is less than expected time\n",
							args->name);
					}
					if (statbuf.st_mtime < tv.tv_sec) {
						pr_fail("%s: utime failed: modified time is less than expected time\n",
							args->name);
					}
				}
			}
			if (shim_utime(filename, NULL) < 0) {
				pr_fail("%s: utime failed: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				break;
			}

			/* Exercise invalid timename, ENOENT */
			ret = shim_utime("", &utbuf);
			(void)ret;

			/* Exercise huge filename, ENAMETOOLONG */
			ret = shim_utime(hugename, &utbuf);
			(void)ret;
		}
#else
		UNEXPECTED
#endif
		/* forces metadata writeback */
		if (utime_fsync) {
			ret = shim_fsync(fd);
			(void)ret;
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW)
	if (dir_fd >= 0)
		(void)close(dir_fd);
#endif
	(void)close(fd);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_utime_fsync,	stress_set_utime_fsync },
	{ 0,			NULL }
};

stressor_info_t stress_utime_info = {
	.stressor = stress_utime,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
