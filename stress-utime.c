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
#include "core-pragma.h"

#include <time.h>

#if defined(HAVE_UTIME_H)
#include <utime.h>
#else
UNEXPECTED
#endif

#define FAT_EPOCH_MAX	4354819200

static const stress_help_t help[] = {
	{ NULL,	"utime N",	"start N workers updating file timestamps" },
	{ NULL,	"utime-fsync",	"force utime meta data sync to the file system" },
	{ NULL,	"utime-ops N",	"stop after N utime bogo operations" },
	{ NULL,	NULL,		NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_utime_fsync, "utime-fsync", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_UTIME_H) &&	\
    defined(HAVE_UTIME) &&	\
    defined(HAVE_UTIMBUF)
static char *stress_utime_str(char *str, const size_t len, const time_t val)
{
	struct tm *tm = localtime(&val);

	if (!tm) {
		/* Just return time in secs since EPOCH */
		(void)snprintf(str, len, "%" PRIdMAX, (intmax_t)val);
		return str;
	}
	(void)strftime(str, len, "%d/%m/%Y %H:%M:%S", tm);
	return str;
}

/*
 *  stress_utime()
 *	stress system by setting file utime
 */
static int OPTIMIZE3 stress_utime(stress_args_t *args)
{
#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW)
	char path[PATH_MAX];
#endif
	int dir_fd = -1;
	int rc = EXIT_SUCCESS;
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
	int metrics_count = 0;

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
		pr_err("%s: open failed, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)stress_temp_dir_rm_args(args);
		if (dir_fd >= 0) /* cppcheck-suppress knownConditionTrueFalse */
			(void)close(dir_fd);
		return ret;
	}

#if defined(HAVE_PATHCONF) &&	\
    defined(_PC_TIMESTAMP_RESOLUTION)
	VOID_RET(long int, pathconf(filename, _PC_TIMESTAMP_RESOLUTION));
#endif
	stress_rndstr(hugename, sizeof(hugename));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(HAVE_UTIMES)
		struct timeval timevals[2];
#endif
#if (defined(HAVE_FUTIMENS) || defined(HAVE_UTIMENSAT)) && \
    (defined(UTIME_NOW) || defined(UTIME_OMIT))
		struct timespec ts[2];
#endif

#if defined(HAVE_UTIMES)
		(void)gettimeofday(&timevals[0], NULL);
		timevals[1] = timevals[0];
		if (LIKELY(metrics_count > 0)) {
			if (UNLIKELY(utimes(filename, timevals) < 0)) {
				pr_dbg("%s: utimes failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
		} else {
			const double t = stress_time_now();

			if (UNLIKELY(utimes(filename, timevals) < 0)) {
				pr_dbg("%s: utimes failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;
		}

		if (LIKELY(metrics_count > 0)) {
			if (UNLIKELY(utimes(filename, NULL) < 0)) {
				pr_dbg("%s: utimes failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
		} else {
			const double t = stress_time_now();

			if (UNLIKELY(utimes(filename, NULL) < 0)) {
				pr_dbg("%s: utimes failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;
		}
#endif

#if defined(HAVE_UTIMES)
		/* Exercise with invalid filename, ENOENT */
		VOID_RET(int, utimes("", timevals));
#endif

#if defined(HAVE_UTIMES)
		/* Exercise huge filename, ENAMETOOLONG */
		VOID_RET(int, utimes(hugename, timevals));
#endif

#if defined(HAVE_UTIMES)
		/* Exercise with time outside FAT time range */
		timevals[0].tv_sec = (time_t)283996800;	/* Monday, 1 January 1979 00:00:00 */
		timevals[0].tv_usec = 0;
		timevals[1].tv_sec = (time_t)283996800;
		timevals[1].tv_usec = 0;
		VOID_RET(int, utimes(filename, timevals));
#endif

#if defined(HAVE_UTIMES) &&	\
    defined(LONG_MAX) &&	\
    (LONG_MAX > 4354819200)
		/* Exercise with time outside FAT time range */
		timevals[0].tv_sec = (time_t)4354819200; /* Monday, 1 January 2108 00:00:00 */
		timevals[0].tv_usec = 0;
		timevals[1].tv_sec = (time_t)4354819200;
		timevals[1].tv_usec = 0;
		VOID_RET(int, utimes(filename, timevals));
#endif

#if defined(HAVE_UTIMES)
		/* Exercise with time outside of UNIX EPOCH  */
		timevals[0].tv_sec = (time_t)2147558400; /* Wednesday 20 January 2038 */
		timevals[0].tv_usec = 0;
		timevals[1].tv_sec = (time_t)2147558400;
		timevals[1].tv_usec = 0;
		VOID_RET(int, utimes(filename, timevals));
#endif

#if defined(HAVE_UTIMES)
		/* Exercise with time before of UNIX EPOCH  */
		timevals[0].tv_sec = (time_t)0x7fffffff;
		timevals[0].tv_usec = 0;
		timevals[1].tv_sec = (time_t)0x7fffffff;
		timevals[1].tv_usec = 0;
		VOID_RET(int, utimes(filename, timevals));
#endif

#if defined(HAVE_UTIMES)
		VOID_RET(int, utimes(filename, NULL));
#endif

#if defined(HAVE_FUTIMENS)
		if (LIKELY(metrics_count > 0)) {
			if (UNLIKELY(futimens(fd, NULL) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
		} else {
			const double t = stress_time_now();

			if (UNLIKELY(futimens(fd, NULL) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;
		}

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
		if (LIKELY(metrics_count > 0)) {
			if (UNLIKELY(futimens(fd, ts) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
		} else {
			const double t = stress_time_now();

			if (UNLIKELY(futimens(fd, ts) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;
		}

		/* Exercise with time outside FAT time range */
		ts[0].tv_sec = (time_t)283996800;	/* Monday, 1 January 1979 00:00:00 */
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = (time_t)283996800;
		ts[1].tv_nsec = 0;
		VOID_RET(int, futimens(fd, ts));

#if defined(LONG_MAX) && (LONG_MAX > 4354819200)
		/* Exercise with time outside FAT time range */
		ts[0].tv_sec = (time_t)4354819200; /* Monday, 1 January 2108 00:00:00 */
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = (time_t)4354819200;
		ts[1].tv_nsec = 0;
		VOID_RET(int, futimens(fd, ts));
#endif

		/* Exercise with time outside of UNIX EPOCH  */
		ts[0].tv_sec = (time_t)2147558400; /* Wednesday 20 January 2038 */
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = (time_t)2147558400;
		ts[1].tv_nsec = 0;
		VOID_RET(int, futimens(fd, ts));

		/* Exercise with time before of UNIX EPOCH  */
		ts[0].tv_sec = (time_t)0x7fffffff;
		ts[0].tv_nsec = 0;
		ts[1].tv_sec = (time_t)0x7fffffff;
		ts[1].tv_nsec = 0;
		VOID_RET(int, futimens(fd, ts));
#else
		UNEXPECTED
#endif

#if defined(UTIME_OMIT)
		ts[0].tv_sec = UTIME_OMIT;
		ts[0].tv_nsec = UTIME_OMIT;
		if (LIKELY(metrics_count > 0)) {
			if (UNLIKELY(futimens(fd, ts) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
		} else {
			const double t = stress_time_now();

			if (UNLIKELY(futimens(fd, ts) < 0)) {
				pr_dbg("%s: futimens failed, errno=%d (%s)%s\n",
					args->name, errno, strerror(errno),
					stress_get_fs_type(filename));
				break;
			}
			duration += stress_time_now() - t;
			count += 1.0;
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

		if (LIKELY(metrics_count > 0)) {
			VOID_RET(int, utimensat(AT_FDCWD, filename, ts, 0));
		} else {
			const double t = stress_time_now();

			VOID_RET(int, utimensat(AT_FDCWD, filename, ts, 0));
			duration += stress_time_now() - t;
			count += 1.0;
		}

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
			int i;

			(void)gettimeofday(&tv, NULL);
			utbuf.actime = (time_t)tv.tv_sec;
			utbuf.modtime = utbuf.actime;

			if (LIKELY(metrics_count > 0)) {
				if (UNLIKELY(utime(filename, &utbuf) < 0)) {
					pr_fail("%s: utime failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno),
						stress_get_fs_type(filename));
					rc = EXIT_FAILURE;
					break;
				}
			} else {
				const double t = stress_time_now();

				if (UNLIKELY(utime(filename, &utbuf) < 0)) {
					pr_fail("%s: utime failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno),
						stress_get_fs_type(filename));
					rc = EXIT_FAILURE;
					break;
				}
				duration += stress_time_now() - t;
				count += 1.0;
			}

			if (verify) {
				struct stat statbuf;

				if (shim_stat(filename, &statbuf) == 0) {
					if (statbuf.st_atime < tv.tv_sec) {
						char t1[64], t2[64];

						pr_fail("%s: utime failed, access time %s is less than expected time %s\n",
							args->name,
							stress_utime_str(t1, sizeof(t1), statbuf.st_atime),
							stress_utime_str(t2, sizeof(t2), tv.tv_sec));
						rc = EXIT_FAILURE;
						break;
					}
					if (statbuf.st_mtime < tv.tv_sec) {
						char t1[64], t2[64];

						pr_fail("%s: utime failed, modified time %s is less than expected time %s\n",
							args->name,
							stress_utime_str(t1, sizeof(t1), statbuf.st_mtime),
							stress_utime_str(t2, sizeof(t2), tv.tv_sec));
						rc = EXIT_FAILURE;
						break;
					}
				}
			}
			if (LIKELY(metrics_count > 0)) {
				if (UNLIKELY(utime(filename, NULL) < 0)) {
					pr_fail("%s: utime failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno),
						stress_get_fs_type(filename));
					rc = EXIT_FAILURE;
					break;
				}
			} else {
				const double t = stress_time_now();

				if (UNLIKELY(utime(filename, NULL) < 0)) {
					pr_fail("%s: utime failed, errno=%d (%s)%s\n",
						args->name, errno, strerror(errno),
						stress_get_fs_type(filename));
					rc = EXIT_FAILURE;
					break;
				}
				duration += stress_time_now() - t;
				count += 1.0;
			}

			/* Exercise invalid timename, ENOENT */
			VOID_RET(int, utime("", &utbuf));

			/* Exercise huge filename, ENAMETOOLONG */
			VOID_RET(int, utime(hugename, &utbuf));

			/* Exercise ranges of +ve times */
			utbuf.actime = (1ULL << ((sizeof(time_t) * 8) - 1)) - 1;
			utbuf.modtime = utbuf.actime;
			VOID_RET(int, utime(filename, &utbuf));
			for (i = 0; utbuf.actime && (i < 64); i++) {
				utbuf.actime >>= 1;
				utbuf.modtime = utbuf.actime;
				VOID_RET(int, utime(filename, &utbuf));
			}

			utbuf.actime = ~(time_t)0;
			utbuf.modtime = utbuf.actime;
			VOID_RET(int, utime(filename, &utbuf));
		}
		/* forces metadata writeback */
		if (utime_fsync) {
			VOID_RET(int, shim_fsync(fd));
		}
#else
		UNEXPECTED
#endif
		if (UNLIKELY(metrics_count++ > 1000))
			metrics_count = 0;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	rate = (duration > 0.0) ? count / duration : 0.0;
	stress_metrics_set(args, 0, "utime calls per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

#if defined(O_DIRECTORY) &&	\
    defined(O_PATH) &&		\
    defined(UTIME_NOW)
	if (dir_fd >= 0)
		(void)close(dir_fd);
#endif
	(void)close(fd);
	(void)shim_unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	return rc;
}

const stressor_info_t stress_utime_info = {
	.stressor = stress_utime,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

const stressor_info_t stress_utime_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_FILESYSTEM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without utime.h"
};

#endif
