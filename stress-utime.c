/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
	{ NULL,	"utime-fsync",	"force utime meta data sync to the file system" },
	{ NULL,	"utime-ops N",	"stop after N utime bogo operations" },
	{ NULL,	NULL,		NULL }
};

static int stress_set_utime_fsync(const char *opt)
{
	return stress_set_setting_true("utime-fsync", opt);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_utime_fsync,	stress_set_utime_fsync },
	{ 0,			NULL }
};

#if defined(HAVE_UTIME_H)

#if defined(HAVE_UTIME) &&	\
    defined(HAVE_UTIMBUF)
static int shim_utime(const char *filename, const struct utimbuf *times)
{
#if defined(__NR_utime) &&	\
    defined(HAVE_SYSCALL)
	return (int)syscall(__NR_utime, filename, times);
#else
	return utime(filename, times);
#endif
}
#endif

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
#if defined(HAVE_UTIME_H) &&	\
    defined(HAVE_UTIME) &&	\
    defined(HAVE_UTIMBUF)
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif
	double duration = 0.0, count = 0.0, rate;

	(void)stress_get_setting("utime-fsync", &utime_fsync);

	ret = stress_temp_dir_mk_args(args);
	if (ret < 0)
		return stress_exit_status(-ret);

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
		ret = stress_exit_status(errno);
		pr_err("%s: open failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		if (dir_fd >= 0) /* cppcheck-suppress knownConditionTrueFalse */
			(void)close(dir_fd);
		return ret;
	}

	stress_rndstr(hugename, sizeof(hugename));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		double t;
		struct timeval timevals[2];
#if (defined(HAVE_FUTIMENS) || defined(HAVE_UTIMENSAT)) && \
    (defined(UTIME_NOW) || defined(UTIME_OMIT))
		struct timespec ts[2];
#endif

		(void)gettimeofday(&timevals[0], NULL);
		timevals[1] = timevals[0];
		t = stress_time_now();
		if (utimes(filename, timevals) < 0) {
			pr_dbg("%s: utimes failed: errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_fs_type(filename));
			break;
		}
		duration += stress_time_now() - t;
		count += 1.0;

		t = stress_time_now();
		if (utimes(filename, NULL) < 0) {
			pr_dbg("%s: utimes failed: errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_fs_type(filename));
			break;
		}
		duration += stress_time_now() - t;
		count += 1.0;

		/* Exercise with invalid filename, ENOENT */
		VOID_RET(int, utimes("", timevals));

		/* Exercise huge filename, ENAMETOOLONG */
		VOID_RET(int, utimes(hugename, timevals));

#if defined(HAVE_FUTIMENS)
		t = stress_time_now();
		if (futimens(fd, NULL) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_fs_type(filename));
			break;
		}
		duration += stress_time_now() - t;
		count += 1.0;

		/* Exercise with invalid fd */
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;
		VOID_RET(int, futimens(-1, ts));

#if defined(UTIME_NOW)
		ts[0].tv_sec = UTIME_NOW;
		ts[0].tv_nsec = UTIME_NOW;
		ts[1].tv_sec = UTIME_NOW;
		ts[1].tv_nsec = UTIME_NOW;
		t = stress_time_now();
		if (futimens(fd, ts) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_fs_type(filename));
			break;
		}
		duration += stress_time_now() - t;
		count += 1.0;
#else
		UNEXPECTED
#endif

#if defined(UTIME_OMIT)
		ts[0].tv_sec = UTIME_OMIT;
		ts[0].tv_nsec = UTIME_OMIT;
		t = stress_time_now();
		if (futimens(fd, ts) < 0) {
			pr_dbg("%s: futimens failed: errno=%d (%s)%s\n",
				args->name, errno, strerror(errno),
				stress_fs_type(filename));
			break;
		}
		duration += stress_time_now() - t;
		count += 1.0;
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

		t = stress_time_now();
		VOID_RET(int, utimensat(AT_FDCWD, filename, ts, 0));
		duration += stress_time_now() - t;
		count += 1.0;

		/* Exercise invalid filename, ENOENT */
		VOID_RET(int, utimensat(AT_FDCWD, "", ts, 0));

		/* Exercise huge filename, ENAMETOOLONG */
		VOID_RET(int, utimensat(AT_FDCWD, hugename, ts, 0));

		/* Exercise invalid flags */
		VOID_RET(int, utimensat(AT_FDCWD, filename, ts, ~0));
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

			VOID_RET(int, utimensat(dir_fd, "", ts, 0));
		}
STRESS_PRAGMA_POP
#endif

#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		VOID_RET(int, utimensat(AT_FDCWD, filename, ts, 0));
#endif


#if defined(AT_SYMLINK_NOFOLLOW)
#if defined(UTIME_NOW)
		ts[1].tv_nsec = UTIME_NOW;
		VOID_RET(int, utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW));
		if (utime_fsync) {
			VOID_RET(int, shim_fsync(fd));
		}
#endif
#if defined(UTIME_OMIT)
		ts[1].tv_nsec = UTIME_OMIT;
		VOID_RET(int, utimensat(AT_FDCWD, filename, ts, AT_SYMLINK_NOFOLLOW));
		if (utime_fsync) {
			VOID_RET(int, shim_fsync(fd));
		}
#endif
#endif
#else
		UNEXPECTED
#endif

#if defined(HAVE_UTIME_H) &&	\
    defined(HAVE_UTIME) &&	\
    defined(HAVE_UTIMBUF)
		{
			struct utimbuf utbuf;
			struct timeval tv;

			(void)gettimeofday(&tv, NULL);
			utbuf.actime = (time_t)tv.tv_sec;
			utbuf.modtime = utbuf.actime;

			t = stress_time_now();
			if (shim_utime(filename, &utbuf) < 0) {
				pr_fail("%s: utime failed: errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;

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
			t = stress_time_now();
			if (shim_utime(filename, NULL) < 0) {
				pr_fail("%s: utime failed: errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;

			/* Exercise invalid timename, ENOENT */
			VOID_RET(int, shim_utime("", &utbuf));

			/* Exercise huge filename, ENAMETOOLONG */
			VOID_RET(int, shim_utime(hugename, &utbuf));
		}
#else
		UNEXPECTED
#endif
		/* forces metadata writeback */
		if (utime_fsync) {
			VOID_RET(int, shim_fsync(fd));
		}
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "utime calls per sec", rate);

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

stressor_info_t stress_utime_info = {
	.stressor = stress_utime,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

stressor_info_t stress_utime_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_FILESYSTEM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without utime.h"
};

#endif
