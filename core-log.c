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

static uint16_t	abort_fails;	/* count of failures */
static bool	abort_msg_emitted;
static FILE	*log_file = NULL;

/*
 *  pr_lock()
 *	attempt to get a lock, it's just too bad
 *	if it fails, this is just to stop messages
 *	getting intermixed.
 */
void pr_lock(bool *lock)
{
#if defined(HAVE_FLOCK) && defined(LOCK_EX) && defined(LOCK_UN)
	int fd, ret;

	*lock = false;

	fd = open("/dev/stdout", O_RDONLY);
	if (fd < 0)
		return;

	ret = flock(fd, LOCK_EX);
	if (ret == 0)
		*lock = true;

	(void)close(fd);
#else
	(void)lock;
#endif
}

/*
 *  pr_unlock()
 *	attempt to unlock
 */
void pr_unlock(bool *lock)
{
#if defined(HAVE_FLOCK) && defined(LOCK_EX) && defined(LOCK_UN)
	int fd, ret;

	if (!*lock)
		return;

	fd = open("/dev/stdout", O_RDONLY);
	if (fd < 0)
		return;

	ret = flock(fd, LOCK_UN);
	if (ret == 0)
		*lock = false;

	(void)close(fd);
#else
	(void)lock;
#endif
}

/*
 *  pr_fail_check()
 *	set rc to EXIT_FAILURE if we detected a pr_fail
 *	error condition in the logging during a run.
 *	This is horribly indirect but it allows the main
 *	stress-ng parent to check that an EXIT_SUCCESS
 *	is really a failure based on the logging rather
 *	that each stressor doing it's own failure exit
 *	return accounting
 */
void pr_fail_check(int *const rc)
{
	if (abort_msg_emitted && (*rc == EXIT_SUCCESS))
		*rc = EXIT_FAILURE;
}

/*
 *  pr_yaml()
 *	print to yaml file if it is open
 */
int pr_yaml(FILE *fp, const char *const fmt, ...)
{
	va_list ap;
	int ret = 0;

	va_start(ap, fmt);
	if (fp)
		ret = vfprintf(fp, fmt, ap);
	va_end(ap);

	return ret;
}

/*
 *  pr_closelog()
 *	log closing
 */
void pr_closelog(void)
{
	if (log_file) {
		(void)fflush(log_file);
		(void)fclose(log_file);
		log_file = NULL;
	}
}

/*
 *  pr_openlog()
 *	optional pr logging to a file
 */
void pr_openlog(const char *filename)
{
	if (!filename)
		return;

	log_file = fopen(filename, "w");
	if (!log_file) {
		pr_err("Cannot open log file %s\n", filename);
		return;
	}
}

static int pr_msg_lockable(
	FILE *fp,
	const uint64_t flag,
	const bool locked,
	const char *const fmt,
	va_list ap) FORMAT(printf, 4, 0);

/*
 *  pr_msg_lockable()
 *	print some debug or info messages with locking
 */
static int pr_msg_lockable(
	FILE *fp,
	const uint64_t flag,
	const bool locked,
	const char *const fmt,
	va_list ap)
{
	int ret = 0;
	char ts[32];

	if (g_opt_flags & OPT_FLAGS_TIMESTAMP) {
		struct timeval tv;
		static char empty_ts[] = "xx-xx-xx.xxx ";

		if (gettimeofday(&tv, NULL) < 0) {
			strncpy(ts, empty_ts, sizeof(ts));
		} else {
			time_t t = tv.tv_sec;
			struct tm *tm;

			tm = localtime(&t);
			if (tm) {
				(void)snprintf(ts, sizeof(ts), "%2.2d:%2.2d:%2.2d.%2.2ld ",
					tm->tm_hour, tm->tm_min, tm->tm_sec,
					(long)tv.tv_usec / 10000);
			} else {
				strncpy(ts, empty_ts, sizeof(ts));
			}
		}
	} else {
		*ts = '\0';
	}

	if ((flag & PR_FAIL) || (g_opt_flags & flag)) {
		char buf[4096];
		const char *type = "";
		bool lock = false;

		if (!locked)
			pr_lock(&lock);

		if (flag & PR_ERROR)
			type = "error:";
		if (flag & PR_DEBUG)
			type = "debug:";
		if (flag & PR_INFO)
			type = "info: ";
		if (flag & PR_FAIL)
			type = "fail: ";

		if (g_opt_flags & OPT_FLAGS_LOG_BRIEF) {
			ret = vfprintf(fp, fmt, ap);
		} else {
			int n = snprintf(buf, sizeof(buf), "%s%s [%d] ",
				ts, type, (int)getpid());
			ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
			(void)fprintf(fp, "%s: %s", g_app_name, buf);
		}
		(void)fflush(fp);

		if (flag & PR_FAIL) {
			abort_fails++;
			if (abort_fails >= ABORT_FAILURES) {
				if (!abort_msg_emitted) {
					abort_msg_emitted = true;
					g_keep_stressing_flag = false;
					(void)fprintf(fp, "info: %d failures "
						"reached, aborting stress "
						"process\n", ABORT_FAILURES);
					(void)fflush(fp);
				}
			}
		}

		/* Log messages to log file if --log-file specified */
		if (log_file) {
			(void)fprintf(log_file, "%s: %s", g_app_name, buf);
			(void)fflush(log_file);
		}

#if defined(HAVE_SYSLOG_H)
		/* Log messages if syslog requested, don't log DEBUG */
		if ((g_opt_flags & OPT_FLAGS_SYSLOG) &&
		    (!(flag & PR_DEBUG))) {
			syslog(LOG_INFO, "%s", buf);
		}

		if (!locked)
			pr_unlock(&lock);
#endif
	}
	return ret;
}

/*
 *  __pr_msg_fail()
 *	wrapper helper for pr_msg_fail
 */
static inline void __pr_msg_fail(const uint64_t flag, const bool locked, char *fmt, ...) FORMAT(printf, 3, 0);

static inline void __pr_msg_fail(const uint64_t flag, const bool locked, char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, flag, locked, fmt, ap);
	va_end(ap);
}

PRAGMA_PUSH
PRAGMA_WARN_OFF
/*
 *  pr_msg_fail()
 *	print failure message with errno
 */
void pr_msg_fail(
	const uint64_t flag,
	const char *name,
	const char *what,
	const int err)
{
	__pr_msg_fail(flag, false, "%s: %s failed, errno=%d (%s)\n",
		name, what, err, strerror(err));
}
PRAGMA_POP

/*
 *  pr_dbg()
 *	print debug messages
 */
void pr_dbg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_DEBUG, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_dbg_lock()
 *	print debug messages with a lock
 */
void pr_dbg_lock(bool *lock, const char *fmt, ...)
{
	va_list ap;

	/* currently we ignore the locked flag */
	(void)lock;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_DEBUG, true, fmt, ap);
	va_end(ap);
}

/*
 *  pr_inf()
 *	print info messages
 */
void pr_inf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_INFO, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_inf()
 *	print info messages with a lock
 */
void pr_inf_lock(bool *lock, const char *fmt, ...)
{
	va_list ap;

	/* currently we ignore the locked flag */
	(void)lock;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_INFO, true, fmt, ap);
	va_end(ap);
}

/*
 *  pr_err()
 *	print error messages
 */
void pr_err(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_ERROR, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_fail()
 *	print failure messages
 */
void pr_fail(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, PR_FAIL, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_tidy()
 *	print tidy up error messages
 */
void pr_tidy(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(stderr, g_caught_sigint ? PR_INFO : PR_DEBUG, true, fmt, ap);
	va_end(ap);
}

/*
 *  pr_fail_err__()
 *	helper for macro pr_fail_err to print error with errno
 */
void pr_fail_err__(const args_t *args, const char *msg)
{
	pr_msg_fail(PR_FAIL | PR_ERROR, args->name, msg, errno);
}

/*
 *  pr_fail_errno__()
 *	helper for macro pr_fail_errno to print error with a given errno
 */
void pr_fail_errno__(const args_t *args, const char *msg, int err)
{
	pr_msg_fail(PR_FAIL | PR_ERROR, args->name, msg, err);
}

/*
 *  pr_fail_dbg__()
 *	helper for macro pr_fail_dbg to print error
 */
void pr_fail_dbg__(const args_t *args, const char *msg)
{
	pr_msg_fail(PR_DEBUG, args->name, msg, errno);
}
