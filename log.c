/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <syslog.h>

#include "stress-ng.h"

static uint16_t	abort_fails;	/* count of failures */
static bool	abort_msg_emitted;
static FILE	*log_file = NULL;

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
 *	log closing, performed via exit()
 */
static void pr_closelog(void)
{
	if (log_file) {
		fclose(log_file);
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
		pr_err(stderr, "Cannot open log file %s\n", filename);
		return;
	}
	atexit(pr_closelog);
}


/*
 *  pr_msg()
 *	print some debug or info messages
 */
int pr_msg(
	FILE *fp,
	const uint64_t flag,
	const char *const fmt, ...)
{
	va_list ap;
	int ret = 0;

	va_start(ap, fmt);
	if ((flag & PR_FAIL) || (opt_flags & flag)) {
		char buf[4096];
		const char *type = "";

		if (flag & PR_ERROR)
			type = "error:";
		if (flag & PR_DEBUG)
			type = "debug:";
		if (flag & PR_INFO)
			type = "info: ";
		if (flag & PR_FAIL)
			type = "fail: ";

		if (opt_flags & OPT_FLAGS_LOG_BRIEF) {
			ret = vfprintf(fp, fmt, ap);
		} else {
			int n = snprintf(buf, sizeof(buf), "%s [%i] ",
				type, getpid());
			ret = vsnprintf(buf + n, sizeof(buf) - n, fmt, ap);
			fprintf(fp, "%s: %s", app_name, buf);
		}
		fflush(fp);

		if (flag & PR_FAIL) {
			abort_fails++;
			if (abort_fails >= ABORT_FAILURES) {
				if (!abort_msg_emitted) {
					abort_msg_emitted = true;
					opt_do_run = false;
					pr_msg(fp, PR_INFO, "%d failures "
						"reached, aborting stress "
						"process\n", ABORT_FAILURES);
					fflush(fp);
				}
			}
		}

		/* Log messages to log file if --log-file specified */
		if (log_file) {
			fprintf(log_file, "%s: %s", app_name, buf);
			fflush(log_file);
		}

		/* Log messages if syslog requested, don't log DEBUG */
		if ((opt_flags & OPT_FLAGS_SYSLOG) &&
		    (!(flag & PR_DEBUG))) {
			syslog(LOG_INFO, "%s", buf);
		}
	}
	va_end(ap);

	return ret;
}

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
	pr_msg(stderr, flag, "%s: %s failed, errno=%d (%s)\n",
		name, what, err, strerror(err));
}

