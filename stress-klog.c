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
#include "core-capabilities.h"

#if defined(__NR_syslog)
#define HAVE_SYSLOG
#endif

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
                pr_inf_skip("%s stressor will be skipped, cannot access klog, "
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
static int stress_klog(stress_args_t *args)
{
	char *buffer;
	ssize_t len;
	const bool klog_capable = stress_check_capability(SHIM_CAP_SYS_ADMIN) ||
				  stress_check_capability(SHIM_CAP_SYSLOG);

	len = shim_klogctl(SYSLOG_ACTION_SIZE_BUFFER, NULL, 0);
	if (len < 0) {
		if (!args->instance) {
			if (errno == EPERM) {
				if (stress_instance_zero(args)) /* cppcheck-suppress knownConditionTrueFalse */
					pr_inf_skip("%s: cannot access syslog buffer, "
						"not permitted, skipping stressor\n",
						args->name);
			} else {
				pr_fail("%s: cannot determine syslog buffer "
					"size, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			}
		}
		return EXIT_NO_RESOURCE;
	}
	if (len == 0) {
		if (!args->instance)
			pr_inf_skip("%s: zero sized syslog buffer, skipping stressor.\n", args->name);
		return EXIT_NO_RESOURCE;
	}
	if (len > (ssize_t)(4 * MB)) {
		if (!args->instance)
			pr_inf("%s: truncating syslog buffer to 4MB\n", args->name);
		len = 4 * MB;
	}
	buffer = (char *)malloc((size_t)len);
	if (!buffer) {
		pr_err("%s: cannot allocate %zu byte syslog buffer%s\n",
			args->name, (size_t)len, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int ret;
		const int buflen = (int)stress_mwc32modn((uint32_t)len) + 1;

		/* Exercise illegal read size */
		(void)shim_klogctl(SYSLOG_ACTION_READ, buffer, -1);
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, buffer, -1);

		/* Exercise illegal buffer */
		(void)shim_klogctl(SYSLOG_ACTION_READ, NULL, 0);
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, NULL, 0);

		/* Exercise zero size read */
		(void)shim_klogctl(SYSLOG_ACTION_READ, buffer, 0);
		(void)shim_klogctl(SYSLOG_ACTION_READ_ALL, buffer, 0);

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

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(buffer);
	return EXIT_SUCCESS;
}

const stressor_info_t stress_klog_info = {
	.stressor = stress_klog,
	.classifier = CLASS_OS,
	.help = help,
	.verify = VERIFY_ALWAYS,
	.supported = stress_klog_supported
};
#else
const stressor_info_t stress_klog_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without syslog() system call or klogctl()"
};
#endif
