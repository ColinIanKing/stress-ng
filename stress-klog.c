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

#include "stress-ng.h"

#if defined(STRESS_KLOG)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>

#define SYSLOG_ACTION_CLOSE		(0)
#define SYSLOG_ACTION_OPEN		(1)
#define SYSLOG_ACTION_READ		(2)
#define SYSLOG_ACTION_READ_ALL		(3)
#define SYSLOG_ACTION_READ_CLEAR	(4)
#define SYSLOG_ACTION_CLEAR		(5)
#define SYSLOG_ACTION_CONSOLE_OFF	(6)
#define SYSLOG_ACTION_CONSOLE_ON	(7)
#define SYSLOG_ACTION_CONSOLE_LEVEL	(8)
#define SYSLOG_ACTION_SIZE_UNREAD	(9)
#define SYSLOG_ACTION_SIZE_BUFFER	(10)

/*
 *  sys_syslog()
 * 	wrapper for syslog system call
 */
static inline int sys_syslog(int type, char *bufp, int len)
{
	return syscall(__NR_syslog, type, bufp, len);
}

/*
 *  stress_klog
 *	stress kernel logging interface
 */
int stress_klog(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	char *buffer;
	ssize_t len;

	(void)instance;

	len  = sys_syslog(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (len < 0) {
		if (!instance)
			pr_err(stderr, "%s: cannot determine syslog buffer "
				"size: errno=%d (%s)\n",
				name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (len == 0) {
		if (!instance)
			pr_err(stderr, "%s: zero sized syslog buffer, aborting.\n", name);
		return EXIT_NO_RESOURCE;
	}
	if (len > (ssize_t)(4 * MB)) {
		if (!instance)
			pr_inf(stderr, "%s: truncating syslog buffer to 4MB\n", name);
		len  = 4 * MB;
	}
	buffer = malloc((size_t)len);
	if (!buffer) {
		pr_err(stderr, "%s: cannot allocate syslog buffer\n", name);
		return EXIT_NO_RESOURCE;
	}

	do {
		int ret, buflen = (mwc32() % len) + 1;

		ret = sys_syslog(SYSLOG_ACTION_READ_ALL, buffer, buflen);
		if (ret < 0)
			pr_fail(stderr, "%s: syslog ACTION_READ_ALL failed: "
				"errno=%d (%s)\n",
				name, errno, strerror(errno));
		if (ret > buflen)
			pr_fail(stderr, "%s: syslog ACTION_READ_ALL returned more "
				"data than was requested.\n", name);

		/* open, no-op, ignore failure */
		(void)sys_syslog(SYSLOG_ACTION_OPEN, NULL, 0);

		/* close, no-op, ignore failure */
		(void)sys_syslog(SYSLOG_ACTION_CLOSE, NULL, 0);

		/* get unread size, ignore failure */
		(void)sys_syslog(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0);

		/* get size of kernel buffer, ignore return */
		(void)sys_syslog(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	free(buffer);
	return EXIT_SUCCESS;
}

#endif
