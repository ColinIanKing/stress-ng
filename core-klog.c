/*
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

#if defined(__linux__)
static pid_t klog_pid = -1;
#endif

void stress_klog_start(void)
{
#if defined(__linux__)
	FILE *klog_fp;

	g_shared->klog_error = false;

	if (!(g_opt_flags & OPT_FLAGS_KLOG_CHECK))
		return;

	klog_fp = fopen("/dev/kmsg", "r");
	if (!klog_fp)
		return;

	klog_pid = fork();
	if (klog_pid == 0) {
		char buf[8192];

		(void)fseek(klog_fp, 0, SEEK_END);

		while (fgets(buf, sizeof(buf), klog_fp)) {
			int priority, facility, n;
			uint64_t timestamp;
			char *ptr;
			char ts[32];
			char *msg = "";

			ptr = strchr(buf, '\n');
			if (ptr)
				*ptr = '\0';

			ptr = strchr(buf, ';');
			if (!ptr)
				continue;
			ptr++;

			n = sscanf(buf, "%d,%d,%" SCNu64, &priority, &facility, &timestamp);
			if (n != 3)
				continue;

			snprintf(ts, sizeof(ts), "[%" PRIu64 ".%6.6" PRIu64 "]",
				timestamp / 1000000, timestamp % 1000000);

			/* Check for CPU throttling messages */
			if ((strstr(buf, "CPU") || strstr(buf, "cpu")) &&
			    (strstr(buf, "throttle") || strstr(buf, "throttling"))) {
				msg = "CPU throttling";
				goto log_info;
			}
			if (strstr(buf, "soft lockup") && strstr(buf, "stuck")) {
				msg = "soft lockup";
				goto log_err;
			}
			if (strstr(buf, "Out of memory")) {
				msg = "out of memory";
				goto log_info;
			}
			if ((priority > 3) && strstr(buf, "OOM")) {
				msg = "out of memory";
				goto log_info;
			}

			switch (priority) {
			case 0:
				msg = "emergency";
				goto log_err;
			case 1:
				msg = "alert";
				goto log_err;
			case 2:
				msg = "critical";
				goto log_err;
			case 3:
				msg = "error";
				goto log_err;
			case 4:
				msg = "warning";
				goto log_info;
			default:
				break;
			}
			continue;

log_info:
			pr_inf("klog-check: %s: %s '%s'\n", msg, ts, ptr);
			continue;
log_err:
			pr_err("klog-check: %s: %s '%s'\n", msg, ts, ptr);
			g_shared->klog_error = true;
		}
	}
	(void)fclose(klog_fp);
#endif
}

void stress_klog_stop(bool *success)
{
#if defined(__linux__)
	if (g_opt_flags & OPT_FLAGS_KLOG_CHECK) {
		if (g_shared->klog_error)
			*success = false;

		if (klog_pid > 1) {
			int status;

			(void)kill(klog_pid, SIGKILL);
			(void)waitpid(klog_pid, &status, 0);
		}
		klog_pid = -1;
		g_shared->klog_error = 0;
	}
#else
	(void)success;
#endif
}
