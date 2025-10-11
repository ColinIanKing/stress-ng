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
#include "core-builtin.h"
#include "core-log.h"
#include "core-syslog.h"

#include <stdarg.h>
#include <time.h>

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

#define ABORT_FAILURES	(5)	/* Number of failures before we abort */

static uint16_t	abort_fails;	/* count of failures */
static bool	abort_msg_emitted;
static int 	log_fd = -1;

/*
 *  This is used per stress-ng process and not shared, so locking is not required
 */
typedef struct {
	pid_t pid;	/* sanity check pid, not locked */
	char *buf;	/* accumulated messages */
} pr_msg_buf_t;

pr_msg_buf_t pr_msg_buf;

int pr_fd(void)
{
	return (g_opt_flags & OPT_FLAGS_STDERR) ? fileno(stderr) : fileno(stdout);
}

/*
 *  pr_log_write_buf_fd()
 *	try to write buf out in as large a chunk as possible, the hope is to
 *	be able to write most data in one go, but fall back to iterative writes
 *	if that's not possible
 */
static void pr_log_write_buf_fd(const int fd, const char *buf, const size_t buf_len)
{
	ssize_t n = (ssize_t)buf_len;
	const char *ptr = buf;

	while (n > 0) {
		ssize_t ret;

		ret = write(fd, ptr, n);
		if (UNLIKELY(ret <= 0))
			break;
		n -= ret;
		ptr += ret;
	}
	/* Only sync if writing to log file */
	if (fd == log_fd)
		shim_fsync(fd);
}

/*
 *  pr_log_write_buf()
 *  	write buf message to log file and tty
 *
 */
static void pr_log_write_buf(const char *buf, const size_t buf_len)
{
	const int fd = pr_fd();

	if (log_fd != -1)
		pr_log_write_buf_fd(log_fd, buf, buf_len);

	pr_log_write_buf_fd(fd, buf, buf_len);
}

/*
 *  pr_log_write()
 *	log message. if pr_begin_block() has been used, buffer the messages
 *	up, otherwise just flush them out immediately.
 */
static void pr_log_write(const char *buf, const size_t buf_len)
{
	const bool buffer_messages = !(g_opt_flags & OPT_FLAGS_LOG_LOCKLESS);

	if (buffer_messages && (pr_msg_buf.pid == getpid())) {
		if (!pr_msg_buf.buf) {
			pr_msg_buf.buf = shim_strdup(buf);
			if (!pr_msg_buf.buf) {
				pr_log_write_buf(buf, buf_len);
				return;
			}
		} else  {
			char *new_buf;
			const size_t len = strlen(pr_msg_buf.buf);

			new_buf = realloc(pr_msg_buf.buf, len + buf_len + 1);
			if (UNLIKELY(!new_buf)) {
				pr_log_write_buf(pr_msg_buf.buf, strlen(pr_msg_buf.buf));
				free(pr_msg_buf.buf);
				pr_msg_buf.buf = NULL;
				pr_log_write_buf(buf, buf_len);
				return;
			}
			pr_msg_buf.buf = new_buf;
			shim_strscpy(pr_msg_buf.buf + len, buf, len + buf_len + 1);
		}
		return;
	}
	pr_log_write_buf(buf, buf_len);
}

/*
 *  pr_block_begin()
 *	start of buffering messages up for a final atomic write
 */
void pr_block_begin(void)
{
	pr_msg_buf.pid = getpid();
	pr_msg_buf.buf = NULL;
}

/*
 *  pr_block_end()
 *	end of buffered block, flush messages out and free buffer
 */
void pr_block_end(void)
{
	if (pr_msg_buf.buf && (pr_msg_buf.pid == getpid())) {
		pr_log_write_buf(pr_msg_buf.buf, strlen(pr_msg_buf.buf));
		free(pr_msg_buf.buf);
		pr_msg_buf.buf = NULL;
		pr_msg_buf.pid = -1;
	}
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
	if (log_fd != -1) {
		shim_fsync(log_fd);
		(void)close(log_fd);
		log_fd = -1;
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

	log_fd = open(filename, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
	if (log_fd < 0) {
		log_fd = -1;
		pr_err("cannot open log file %s, errno=%d (%s)\n", filename, errno, strerror(errno));
		return;
	}
}

static int pr_msg(
	const uint64_t flag,
	const char *const fmt,
	va_list ap) FORMAT(printf, 2, 0);
/*
 *  pr_msg_lockable()
 *	print some debug or info messages with locking
 */
static int pr_msg(
	const uint64_t flag,
	const char *const fmt,
	va_list ap)
{
	int ret = 0;
	char ts[32];
	pid_t pid = getpid();

	if (g_opt_flags & OPT_FLAGS_TIMESTAMP) {
		struct timeval tv;
		static const char empty_ts[] = "xx-xx-xx.xxx ";

		if (gettimeofday(&tv, NULL) < 0) {
			strncpy(ts, empty_ts, sizeof(ts));
		} else {
#if defined(HAVE_LOCALTIME_R)
			time_t t = tv.tv_sec;
			struct tm tm;

			(void)localtime_r(&t, &tm);

			(void)snprintf(ts, sizeof(ts), "%2.2d:%2.2d:%2.2d.%2.2ld ",
				tm.tm_hour, tm.tm_min, tm.tm_sec,
				(long int)tv.tv_usec / 10000);
#else
			strncpy(ts, empty_ts, sizeof(ts));
#endif
		}
	} else {
		*ts = '\0';
	}

	if ((flag & (OPT_FLAGS_PR_FAIL | OPT_FLAGS_PR_WARN)) || (g_opt_flags & flag)) {
		char buf[8192];
		const char *type = "";

		if (flag & OPT_FLAGS_PR_ERROR)
			type = "error:";
		if (flag & OPT_FLAGS_PR_DEBUG)
			type = "debug:";
		if (flag & OPT_FLAGS_PR_INFO)
			type = "info: ";
		if (flag & OPT_FLAGS_PR_FAIL)
			type = "fail: ";
		if (flag & OPT_FLAGS_PR_WARN)
			type = "warn: ";
		if (flag & OPT_FLAGS_PR_METRICS)
			type = "metrc:";

		if (g_opt_flags & OPT_FLAGS_LOG_BRIEF) {
			const size_t n = (size_t)vsnprintf(buf, sizeof(buf), fmt, ap);

			pr_log_write(buf, n);
		} else {
			const size_t n = (size_t)snprintf(buf, sizeof(buf), "%s: %s%s [%" PRIdMAX "] ",
				g_app_name, ts, type, (intmax_t)pid);
			size_t len;

			ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
			len = strlen(buf);
			pr_log_write(buf, len);
		}

		if (flag & OPT_FLAGS_PR_FAIL) {
			abort_fails++;
			if (abort_fails >= ABORT_FAILURES) {
				if (!abort_msg_emitted) {
					size_t len;

					abort_msg_emitted = true;
					stress_continue_set_flag(false);

					(void)snprintf(buf, sizeof(buf), "%s: %s%s [%" PRIdMAX "] "
						"info: %d failures reached, aborting stress process\n",
						g_app_name, ts, type, (intmax_t)pid, ABORT_FAILURES);
					len = strlen(buf);
					pr_log_write(buf, len);
				}
			}
		}

#if defined(HAVE_SYSLOG_H)
		/* Log messages if syslog requested, don't log DEBUG */
		if ((g_opt_flags & OPT_FLAGS_SYSLOG) &&
		    (!(flag & OPT_FLAGS_PR_DEBUG))) {
			syslog(LOG_INFO, "%s", buf);
		}
#endif
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
	(void)pr_msg(OPT_FLAGS_PR_DEBUG, fmt, ap);
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
		(void)pr_msg(OPT_FLAGS_PR_DEBUG, fmt, ap);
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
	(void)pr_msg(OPT_FLAGS_PR_INFO, fmt, ap);
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
		(void)pr_msg(OPT_FLAGS_PR_INFO, fmt, ap);
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
	(void)pr_msg(OPT_FLAGS_PR_ERROR, fmt, ap);
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
		(void)pr_msg(OPT_FLAGS_PR_ERROR, fmt, ap);
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
	(void)pr_msg(OPT_FLAGS_PR_FAIL, fmt, ap);
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
	(void)pr_msg(g_shared && g_shared->caught_sigint ? OPT_FLAGS_PR_INFO : OPT_FLAGS_PR_DEBUG, fmt, ap);
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
	(void)pr_msg(OPT_FLAGS_PR_WARN, fmt, ap);
	va_end(ap);
}

/*
 *  pr_warn_skip()
 *	print warn message, don't print if skip silent is enabled
 */
void pr_warn_skip(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	if (!(g_opt_flags & OPT_FLAGS_SKIP_SILENT))
		(void)pr_msg(OPT_FLAGS_PR_WARN, fmt, ap);
	va_end(ap);
}

/*
 *  pr_metrics()
 *	print metrics messages
 */
void pr_metrics(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void)pr_msg(OPT_FLAGS_PR_METRICS, fmt, ap);
	va_end(ap);
}
