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

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

static uint16_t	abort_fails;	/* count of failures */
static bool	abort_msg_emitted;
static FILE	*log_file = NULL;

static inline FILE *pr_file(void)
{
	return (g_opt_flags & OPT_FLAGS_STDOUT) ? stdout : stderr;
}

/*
 *  pr_lock()
 *	attempt to get a lock, it's just too bad
 *	if it fails, this is just to stop messages
 *	getting intermixed.
 */
void pr_lock(bool *lock)
{
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
	int fd, ret;

	*lock = false;

	fd = fileno(pr_file());
	if (fd < 0)
		return;

	ret = flock(fd, LOCK_EX);
	if (ret == 0)
		*lock = true;
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
#if defined(HAVE_FLOCK) &&	\
    defined(LOCK_EX) &&		\
    defined(LOCK_UN)
	int fd, ret;

	if (!*lock)
		return;

	fd = fileno(pr_file());
	if (fd < 0)
		return;

	ret = flock(fd, LOCK_UN);
	if (ret == 0)
		*lock = false;
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
		static const char empty_ts[] = "xx-xx-xx.xxx ";

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
		if (flag & PR_WARN)
			type = "warn: ";

		if (g_opt_flags & OPT_FLAGS_LOG_BRIEF) {
			size_t n = (size_t)vsnprintf(buf, sizeof(buf), fmt, ap);
			if (log_file) {
				VOID_RET(size_t, fwrite(buf, 1, n, log_file));
				(void)fflush(log_file);
			}
			VOID_RET(size_t, fwrite(buf, 1, n, fp));
		} else {
			size_t n = (size_t)snprintf(buf, sizeof(buf), "%s%s [%d] ",
				ts, type, (int)getpid());
			ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
			(void)fprintf(fp, "%s: %s", g_app_name, buf);
			if (log_file) {
				(void)fprintf(log_file, "%s: %s", g_app_name, buf);
				(void)fflush(log_file);
			}
		}
		(void)fflush(fp);

		if (flag & PR_FAIL) {
			abort_fails++;
			if (abort_fails >= ABORT_FAILURES) {
				if (!abort_msg_emitted) {
					abort_msg_emitted = true;
					keep_stressing_set_flag(false);
					(void)fprintf(fp, "info: %d failures "
						"reached, aborting stress "
						"process\n", ABORT_FAILURES);
					(void)fflush(fp);
				}
			}
		}

#if defined(HAVE_SYSLOG_H)
		/* Log messages if syslog requested, don't log DEBUG */
		if ((g_opt_flags & OPT_FLAGS_SYSLOG) &&
		    (!(flag & PR_DEBUG))) {
			syslog(LOG_INFO, "%s", buf);
		}
#endif
		if (!locked)
			pr_unlock(&lock);
	}
	return ret;
}

/*
 *  pr_dbg()
 *	print debug messages
 */
void pr_dbg(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(pr_file(), PR_DEBUG, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_dbg_info()
 *	print debug message, don't print if skip silent is enabled
 */
void pr_dbg_skip(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!(g_opt_flags & OPT_FLAGS_SKIP_SILENT))
		(void)pr_msg_lockable(pr_file(), PR_DEBUG, false, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), PR_DEBUG, true, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), PR_INFO, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_inf_skip()
 *	print info message, don't print if skip silent is enabled
 */
void pr_inf_skip(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!(g_opt_flags & OPT_FLAGS_SKIP_SILENT))
		(void)pr_msg_lockable(pr_file(), PR_INFO, false, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), PR_INFO, true, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), PR_ERROR, false, fmt, ap);
	va_end(ap);
}

/*
 *  pr_err_skip()
 *	print error messages, don't print if skip silent is enabled
 */
void pr_err_skip(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!(g_opt_flags & OPT_FLAGS_SKIP_SILENT))
		(void)pr_msg_lockable(pr_file(), PR_ERROR, false, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), PR_FAIL, false, fmt, ap);
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
	(void)pr_msg_lockable(pr_file(), g_caught_sigint ? PR_INFO : PR_DEBUG, true, fmt, ap);
	va_end(ap);
}

/*
 *  pr_warn()
 *	print warning messages
 */
void pr_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg_lockable(pr_file(), PR_WARN, false, fmt, ap);
	va_end(ap);
}

