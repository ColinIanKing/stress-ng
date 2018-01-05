/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

	if (g_opt_flags & OPT_FLAGS_OOMABLE)
		make_killable = true;

	/*
	 *  Try modern oom interface
	 */
	if ((fd = open("/proc/self/oom_score_adj", O_WRONLY)) >= 0) {
		char *str;
		ssize_t n;

		if (make_killable)
			str = OOM_SCORE_ADJ_MAX;
		else
			str = high_priv ? OOM_SCORE_ADJ_MIN : "0";

redo_wr1:
		n = write(fd, str, strlen(str));
		if (n <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr1;
			if (errno)
				pr_dbg("%s: can't set oom_score_adj\n", name);
		}
		(void)close(fd);
		return;
	}
	/*
	 *  Fall back to old oom interface
	 */
	if ((fd = open("/proc/self/oom_adj", O_WRONLY)) >= 0) {
		char *str;
		ssize_t n;

		if (make_killable)
			str = high_priv ? OOM_ADJ_NO_OOM : OOM_ADJ_MIN;
		else
			str = OOM_ADJ_MAX;

redo_wr2:
		n = write(fd, str, strlen(str));
		if (n <= 0) {
			if ((errno == EAGAIN) || (errno == EINTR))
				goto redo_wr2;
			if (errno)
				pr_dbg("%s: can't set oom_adj\n", name);
		}
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
#endif
