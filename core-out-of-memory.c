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

#if defined(__linux__)

#define OOM_SCORE_ADJ_MIN	"-1000"
#define OOM_SCORE_ADJ_MAX	"1000"

#define OOM_ADJ_NO_OOM		"-17"
#define OOM_ADJ_MIN		"-16"
#define OOM_ADJ_MAX		"15"

/*
 *  process_oomed()
 *	check if a process has been logged as OOM killed
 */
bool process_oomed(const pid_t pid)
{
	int fd;
	bool oomed = false;

	fd = open("/dev/kmsg", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return oomed;

	for (;;) {
		char buf[4096], *ptr;
		ssize_t ret;

		ret = read(fd, buf, sizeof(buf) - 1);
		if (ret < 0)
			break;
		buf[ret] = '\0';

		/*
		 * Look for 'Out of memory: Kill process 22566'
		 */
		ptr = strstr(buf, "process");
		if (ptr && (strstr(buf, "Out of memory") ||
			    strstr(buf, "oom_reaper"))) {
			pid_t oom_pid;

			if (sscanf(ptr + 7, "%10d", &oom_pid) == 1) {
				if (oom_pid == pid) {
					oomed = true;
					break;
				}
			}
		}
	}
	(void)close(fd);

	return oomed;
}

/*
 *    set_adjustment()
 *	try to set OOM adjustment, retry if EAGAIN or EINTR, give up
 *	after multiple retries.
 */
static void set_adjustment(const char *name, const int fd, const char *str)
{
	const size_t len = strlen(str);
	int i;

	for (i = 0; i < 32; i++) {
		ssize_t n;

		n = write(fd, str, len);
		if (n > 0)
			return;

		if ((errno != EAGAIN) && (errno != EINTR)) {
			pr_dbg("%s: can't set oom_score_adj\n", name);
			return;
		}
	}
	/* Unexpected failure, report why */
	pr_dbg("%s: can't set oom_score_adj, errno=%d (%s)\n", name,
		errno, strerror(errno));
}

/*
 *  set_oom_adjustment()
 *	attempt to stop oom killer
 *	if we have root privileges then try and make process
 *	unkillable by oom killer
 */
void set_oom_adjustment(const char *name, const bool killable)
{
	int fd;
	bool high_priv;
	bool make_killable = killable;

	high_priv = (getuid() == 0) && (geteuid() == 0);

	/*
	 *  main cannot be killable; if OPT_FLAGS_OOMABLE set make
	 *  all child procs easily OOMable
	 */
	if (!strcmp(name, "main") && (g_opt_flags & OPT_FLAGS_OOMABLE))
		make_killable = true;

	/*
	 *  Try modern oom interface
	 */
	if ((fd = open("/proc/self/oom_score_adj", O_WRONLY)) >= 0) {
		char *str;

		if (make_killable)
			str = OOM_SCORE_ADJ_MAX;
		else
			str = high_priv ? OOM_SCORE_ADJ_MIN : "0";

		set_adjustment(name, fd, str);
		(void)close(fd);
		return;
	}
	/*
	 *  Fall back to old oom interface
	 */
	if ((fd = open("/proc/self/oom_adj", O_WRONLY)) >= 0) {
		char *str;

		if (make_killable)
			str = high_priv ? OOM_ADJ_NO_OOM : OOM_ADJ_MIN;
		else
			str = OOM_ADJ_MAX;

		set_adjustment(name, fd, str);
		(void)close(fd);
	}
	return;
}
#else
void set_oom_adjustment(const char *name, const bool killable)
{
	(void)name;
	(void)killable;
}
bool process_oomed(const pid_t pid)
{
	(void)pid;

	return false;
}
#endif
