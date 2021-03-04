/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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

static const stress_help_t help[] = {
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
 *  stress_klog_supported()
 *      check if we can access klog
 */
static int stress_klog_supported(const char *name)
{
	if (shim_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0) < 0) {
                pr_inf("%s stressor will be skipped, cannot access klog, "
                        "probably need to be running with CAP_SYS_ADMIN "
                        "rights for this stressor\n", name);
                return -1;
        }
        return 0;
}

/*
 *  stress_klog
 *	stress kernel logging interface
 */
static int stress_klog(const stress_args_t *args)
{
	char *buffer;
	ssize_t len;
	const bool klog_capable = stress_check_capability(SHIM_CAP_SYS_ADMIN) |
				  stress_check_capability(SHIM_CAP_SYSLOG);

	len = shim_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (len < 0) {
		if (!args->instance) {
			if (errno == EPERM) {
				pr_inf("%s: cannot access syslog buffer, "
					"not permitted, skipping stressor\n",
					args->name);
			} else {
				pr_err("%s: cannot determine syslog buffer "
					"size: errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}
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
		len = 4 * MB;
	}
	buffer = malloc((size_t)len);
	if (!buffer) {
		pr_err("%s: cannot allocate syslog buffer\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret, buflen = (stress_mwc32() % len) + 1;

		ret = shim_klogctl(SYSLOG_ACTION_READ_ALL, buffer, buflen);
		if (ret < 0)
			pr_fail("%s: syslog ACTION_READ_ALL failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
		if (ret > buflen)
			pr_fail("%s: syslog ACTION_READ_ALL returned more "
				"data than was requested.\n", args->name);

		/* open, no-op, ignore failure */
		(void)shim_klogctl(SYSLOG_ACTION_OPEN, NULL, 0);

		/* close, no-op, ignore failure */
		(void)shim_klogctl(SYSLOG_ACTION_CLOSE, NULL, 0);

		/* get unread size, ignore failure */
		(void)shim_klogctl(SYSLOG_ACTION_SIZE_UNREAD, NULL, 0);

		/* get size of kernel buffer, ignore return */
		(void)shim_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);

		/*
		 *  exercise klog commands that only can be performed with
		 *  the correct capability without that capability to force
		 *  -EPERM errors
		 */
		if (!klog_capable) {
			(void)shim_klogctl(SYSLOG_ACTION_CLEAR, NULL, 0);
			(void)shim_klogctl(SYSLOG_ACTION_READ_CLEAR, buffer, buflen);
			(void)shim_klogctl(SYSLOG_ACTION_CONSOLE_OFF, NULL, 0);
			(void)shim_klogctl(SYSLOG_ACTION_CONSOLE_ON, NULL, 0);
		}

		/* set invalid console levels */
		(void)shim_klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, -1);
		(void)shim_klogctl(SYSLOG_ACTION_CONSOLE_LEVEL, NULL, 0x7ffffff);

		/* invalid command type */
		(void)shim_klogctl(-1, NULL, 0);

		/* invalid buffer */
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, NULL, buflen);

		/* invalid lengths */
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, buffer, -1);

		/* unusual length */
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, buffer, 0);

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(buffer);
	return EXIT_SUCCESS;
}

stressor_info_t stress_klog_info = {
	.stressor = stress_klog,
	.class = CLASS_OS,
	.help = help,
	.supported = stress_klog_supported
};
#else
stressor_info_t stress_klog_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_OS,
	.help = help
};
#endif
