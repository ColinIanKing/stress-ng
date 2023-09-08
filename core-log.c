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
#include "core-log.h"
#include "core-syslog.h"

#define PR_TIMEOUT	(0.5)	/* pr_lock timeout in seconds */

#if defined(HAVE_SYSLOG_H)
#include <syslog.h>
#endif

static uint16_t	abort_fails;	/* count of failures */
static bool	abort_msg_emitted;
static FILE	*log_file = NULL;

static inline FILE *pr_file(void)
{
	return (g_opt_flags & OPT_FLAGS_STDERR) ? stderr : stdout;
}

/*
 *  pr_log_file_flush()
 *	ensure log data is flushed as much as possible
 */
static void pr_log_file_flush(void)
{

	if (log_file) {
		const int fd = fileno(log_file);

		(void)fflush(log_file);
		if (fd >= 0)
			shim_fsync(fd);
	}
}

#if defined(HAVE_ATOMIC_COMPARE_EXCHANGE) &&	\
    defined(HAVE_ATOMIC_STORE)

/*
 *  pr_lock_usable()
 *	inline helper, return true if locking is usable
 */
static inline bool pr_lock_usable(void)
{
	/* lockless logging - not lockable */
	if (g_opt_flags & OPT_FLAGS_LOG_LOCKLESS)
		return false;
	/* something wrong with shared data? */
	if (!g_shared)
		return false;
	/* locking is usable */
	return true;
}

/*
 *  pr_lock_init()
 */
void pr_lock_init(void)
{
	if (!pr_lock_usable())
		return;

	g_shared->pr.pid = -1;
	g_shared->pr.lock_count = 0;
	g_shared->pr.atomic_lock = 0;
	g_shared->pr.whence = -1.0;
}

/*
 *  pr_spin_lock_pid()
 *	simple spin lock, if pid is -1, getpid
 */
static void pr_spin_lock_pid(pid_t pid)
{
	double timeout_time;
	pid_t val, orig;

	if (!pr_lock_usable())
		return;

	timeout_time = stress_time_now() + PR_TIMEOUT;
	if (pid == (pid_t)-1)
		pid = getpid();

	for (;;) {
		while (stress_time_now() < timeout_time) {
			orig = 0;
			val = pid;
			if (__atomic_compare_exchange(&g_shared->pr.atomic_lock, &orig,
			    &val, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST))
				return;
			/* Owner dead? force unlock and retry */
			if ((shim_kill(orig, 0) < 0) && (errno == ESRCH)) {
				val = 0;
				__atomic_exchange(&g_shared->pr.atomic_lock, &val, &orig, __ATOMIC_SEQ_CST);
				continue;
			}
			stress_yield_sleep_ms();
		}

		/*
		 *  Owner won't let go of spinlock, we force the lock to be unlocked
		 *  and re-try. Urgh.
		 */
		val = 0;
		__atomic_exchange(&g_shared->pr.atomic_lock, &val, &orig, __ATOMIC_SEQ_CST);
		/* reset the timeout */
		timeout_time = stress_time_now() + PR_TIMEOUT;
	}
}

/*
 *  pr_spin_unlock()
 *	spin unlock
 */
static void pr_spin_unlock(void)
{
	int zero;

	if (!pr_lock_usable())
		return;

	zero = 0;
	__atomic_store(&g_shared->pr.atomic_lock, &zero, __ATOMIC_SEQ_CST);
}

/*
 *  pr_lock_acquire()
 *	acquire a pr lock in a timely way, force lock ownership
 *	if we wait for too long
 */
static void pr_lock_acquire(const pid_t pid)
{
	if (!pr_lock_usable())
		return;

	for (;;) {
		int32_t count;
		double whence, now;

		pr_spin_lock_pid(pid);
		count = g_shared->pr.lock_count;
		whence = g_shared->pr.whence;
		now = stress_time_now();

		/*
		 * Locks held for too long are broken and holding logging
		 * up, so pass ownership over
		 */
		if (((now - whence) > PR_TIMEOUT) || count == 0) {
			g_shared->pr.pid = pid;	/* Indicate we now own lock */
			g_shared->pr.lock_count++;
			g_shared->pr.whence = now;
			pr_spin_unlock();
			return;
		}
		pr_spin_unlock();
		shim_usleep(100000);
	}
}

/*
 *  pr_lock_pid()
 *	attempt to get a lock, it's just too bad
 *	if it fails, this is just to stop messages
 *	getting intermixed.
 *
 *  pr_lock_pid can be called multiple times by a process, only
 *  the first call acquires the lock
 */
static void pr_lock_pid(pid_t pid)
{
	double now;

	if (!pr_lock_usable())
		return;

	if (pid == (pid_t)-1)
		pid = getpid();
	pr_spin_lock_pid(pid);
	/* Already own lock? */
	if (g_shared->pr.pid == pid) {
		g_shared->pr.lock_count++;
		pr_spin_unlock();
		return;
	}

	if (g_shared->pr.pid == -1) {
		/* No owner, acquire lock */
		g_shared->pr.pid = pid;
		pr_spin_unlock();
		pr_lock_acquire(pid);
		return;
	}

	/*
	 *  Sanity check, has the lock been owned for too long
	 *  or has the owenr pid died, then force unlock and
	 *  take ownership.
	 */
	now = stress_time_now();
	if (((now - g_shared->pr.whence) > PR_TIMEOUT) ||
	    (shim_kill(g_shared->pr.pid, 0) == ESRCH)) {
		/* force acquire */
		g_shared->pr.pid = pid;
		g_shared->pr.lock_count = 0;
		g_shared->pr.whence = now;
		pr_spin_unlock();
		return;
	}
	/* Wait on lock */
	pr_spin_unlock();
	pr_lock_acquire(pid);
}

void pr_lock(void)
{
	pr_lock_pid((pid_t)-1);
}

/*
 *  pr_unlock_pid()
 *	attempt to unlock
 */
static void pr_unlock_pid(pid_t pid)
{
	if (!pr_lock_usable())
		return;

	if (pid == (pid_t)-1)
		pid = getpid();
	pr_spin_lock_pid(pid);
	/* Do we own the lock? */
	if (g_shared->pr.pid == pid) {
		g_shared->pr.lock_count--;
		if (g_shared->pr.lock_count == 0) {
			g_shared->pr.pid = -1;
			g_shared->pr.whence = -1.0;
		}
	}
	pr_spin_unlock();
}

void pr_unlock(void)
{
	pr_unlock_pid((pid_t)-1);
}

/*
 *  pr_lock_exited()
 *	process has terminated, ensure locks are cleared on it
 */
void pr_lock_exited(const pid_t pid)
{
	if (!pr_lock_usable())
		return;

	pr_spin_lock_pid(pid);
	if (g_shared->pr.pid == pid) {
		g_shared->pr.pid = -1;
		g_shared->pr.lock_count = 0;
		g_shared->pr.whence = 0.0;
	}
	pr_spin_unlock();
}
#else

static inline bool pr_lock_usable(void)
{
	return false;
}

void pr_lock_init(void)
{
}

static void pr_lock_pid(pid_t pid)
{
	(void)pid;
}

void pr_lock(void)
{
}

static void pr_unlock_pid(pid_t pid)
{
	(void)pid;
}

void pr_unlock(void)
{
}

void pr_lock_exited(const pid_t pid)
{
	(void)pid;
}

#endif

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

static int pr_msg(
	FILE *fp,
	const uint64_t flag,
	const char *const fmt,
	va_list ap) FORMAT(printf, 3, 0);
/*
 *  pr_msg_lockable()
 *	print some debug or info messages with locking
 */
static int pr_msg(
	FILE *fp,
	const uint64_t flag,
	const char *const fmt,
	va_list ap)
{
	int ret = 0;
	char ts[32];
	pid_t pid = getpid();

	pr_lock_pid(pid);

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

	if ((flag & (OPT_FLAGS_PR_FAIL | OPT_FLAGS_PR_WARN)) || (g_opt_flags & flag)) {
		char buf[4096];
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
			size_t n = (size_t)vsnprintf(buf, sizeof(buf), fmt, ap);
			if (log_file) {
				VOID_RET(size_t, fwrite(buf, 1, n, log_file));
				pr_log_file_flush();
			}
			VOID_RET(size_t, fwrite(buf, 1, n, fp));
		} else {
			size_t n = (size_t)snprintf(buf, sizeof(buf), "%s%s [%" PRIdMAX "] ",
				ts, type, (intmax_t)pid);
			ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
			(void)fprintf(fp, "%s: %s", g_app_name, buf);
			if (log_file) {
				(void)fprintf(log_file, "%s: %s", g_app_name, buf);
				pr_log_file_flush();
			}
		}
		(void)fflush(fp);

		if (flag & OPT_FLAGS_PR_FAIL) {
			abort_fails++;
			if (abort_fails >= ABORT_FAILURES) {
				if (!abort_msg_emitted) {
					abort_msg_emitted = true;
					stress_continue_set_flag(false);
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
		    (!(flag & OPT_FLAGS_PR_DEBUG))) {
			syslog(LOG_INFO, "%s", buf);
		}
#endif
	}
	pr_unlock_pid(pid);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_DEBUG, fmt, ap);
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
		(void)pr_msg(pr_file(), OPT_FLAGS_PR_DEBUG, fmt, ap);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_INFO, fmt, ap);
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
		(void)pr_msg(pr_file(), OPT_FLAGS_PR_INFO, fmt, ap);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_ERROR, fmt, ap);
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
		(void)pr_msg(pr_file(), OPT_FLAGS_PR_ERROR, fmt, ap);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_FAIL, fmt, ap);
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
	(void)pr_msg(pr_file(), g_shared && g_shared->caught_sigint ? OPT_FLAGS_PR_INFO : OPT_FLAGS_PR_DEBUG, fmt, ap);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_WARN, fmt, ap);
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
		(void)pr_msg(pr_file(), OPT_FLAGS_PR_WARN, fmt, ap);
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
	(void)pr_msg(pr_file(), OPT_FLAGS_PR_METRICS, fmt, ap);
	va_end(ap);
}
