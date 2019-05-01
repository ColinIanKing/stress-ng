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

static const help_t help[] = {
	{ NULL,	"klog N",	"start N workers exercising kernel syslog interface" },
	{ NULL,	"klog-ops N",	"stop after N klog bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SYSLOG)

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
 *  stress_klog
 *	stress kernel logging interface
 */
static int stress_klog(const args_t *args)
{
	char *buffer;
	ssize_t len;

	len  = shim_syslog(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (len < 0) {
		if (!args->instance)
			pr_err("%s: cannot determine syslog buffer "
				"size: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	if (len == 0) {
		if (!args->instance)
			pr_err("%s: zero sized syslog buffer, aborting.\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	if (len > (ssize_t)(4 * MB)) {
		if (!args->instance)
			pr_inf("%s: truncating syslog buffer to 4MB\n", args->name);
		len  = 4 * MB;
	}
	buffer = malloc((size_t)len);
	if (!buffer) {
		pr_err("%s: cannot allocate syslog buffer\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	do {
		int ret, buflen = (mwc32() % len) + 1;

		ret = shim_syslog(SYSLOG_ACTION_READ_ALL, buffer, buflen);
		if (ret < 0)
			pr_fail_err("syslog ACTION_READ_ALL");
		if (ret > buflen)
			pr_fail("%s: syslog ACTION_READ_ALL returned more "
				"data than was requested.\n", args->name);

		/* open, no-op, ignore failure */
		(void)shim_syslog(SYSLOG_ACTION_OPEN, NULL, 0);

		/* close, no-op, ignore failure */
		(void)shim_syslog(SYSLOG_ACTION_CLOSE, NULL, 0);

		/* get unread size, ignore failure */
		(void)shim_syslog(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0);

		/* get size of kernel buffer, ignore return */
		(void)shim_syslog(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

		inc_counter(args);
	} while (keep_stressing());

	free(buffer);
	return EXIT_SUCCESS;
}

stressor_info_t stress_klog_info = {
	.stressor = stress_klog,
	.class = CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_klog_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
