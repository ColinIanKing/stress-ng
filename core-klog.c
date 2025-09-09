/*
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
#include "core-attribute.h"
#include "core-killpid.h"
#include "core-klog.h"
#include "core-processes.h"

#include <sched.h>

#if defined(__linux__)
static pid_t klog_pid = -1;

static const char name[] = "klog-check";

/*
 *  strings that are to be ignored as an error
 */
static const char * const err_exceptions[] = {
	"audit: backlog",
	"x86/split lock detection",
	"detected capacity change from",
	"umip_printk",
	"expecting 0xbadc0de (pid=",
	"callbacks suppressed",
	"kmod_concurrent_max",
	"hrtimer: interrupt took",
	"no longer affine to",
};

/*
 *  stress_klog_err_no_exceptions()
 *	check for str in the err_exceptions array, returns
 *	false if a match is found, and hence can be ignored
 *	as an error.
 */
static bool CONST stress_klog_err_no_exceptions(const char *str)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(err_exceptions); i++) {
		if (strstr(str, err_exceptions[i]))
			return false;
	}
	return true;
}
#endif

#if defined(__linux__)
/*
 *  stress_klog_kernel_cmdline()
 *	where possible log kernel command line, just once
 */
static void stress_klog_kernel_cmdline(void)
{
	static bool already_dumped = false;
	char buffer[4096], *ptr;
	ssize_t ret;

	if (already_dumped)
		return;

	ret = stress_system_read("/proc/cmdline", buffer, sizeof(buffer));
	if (ret < 0)
		return;

	for (ptr = buffer; *ptr && (ptr < (buffer + ret)); ptr++) {
		if (*ptr == '\n') {
			*ptr = '\0';
			break;
		}
	}
	pr_inf("%s: kernel cmdline: '%s'\n", name, buffer);
	already_dumped = true;
}
#endif

#if defined(__linux__)
/*
 *  stress_klog_convert_nl()
 *	convert escaped nl to space
 */
static void stress_klog_convert_nl(char *buf)
{
	for (;;) {
		char *ptr = strstr(buf, "\\x0a");

		if (!ptr)
			return;

		*(ptr++) = ' ';
		while (*ptr) {
			*ptr = *(ptr + 3);
			ptr++;
		}
	}
}
#endif

/*
 *  stress_klog_start()
 *	start a child process that monitors kernel log
 *	messages and logs them if they look concerning
 */
void stress_klog_start(void)
{
#if defined(__linux__)
	FILE *klog_fp;

	g_shared->klog_errors = 0;

	if (!(g_opt_flags & OPT_FLAGS_KLOG_CHECK))
		return;

	klog_fp = fopen("/dev/kmsg", "r");
	if (!klog_fp)
		return;

	klog_pid = fork();
	if (klog_pid == 0) {
		char buf[8192];
		double last_logged = stress_time_now();

		stress_parent_died_alarm();
		stress_set_proc_state_str("klog","monitoring");

		VOID_RET(int, stress_set_sched(getpid(), SCHED_RR, UNDEFINED, true));
		(void)fseek(klog_fp, 0, SEEK_END);

		while (fgets(buf, sizeof(buf), klog_fp)) {
			int priority, facility, n;
			uint64_t timestamp;
			char *ptr;
			char ts[32];
			char *msg;
			bool dump_procs = false;

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

			(void)snprintf(ts, sizeof(ts), "[%" PRIu64 ".%6.6" PRIu64 "]",
				timestamp / 1000000, timestamp % 1000000);

			stress_klog_convert_nl(buf);

			if (strstr(buf, "audit:")) {
				msg = "audit";
				goto log_info;
			}

			/* Check for CPU throttling messages */
			if ((strstr(buf, "CPU") || strstr(buf, "cpu")) &&
			    (strstr(buf, "throttle") || strstr(buf, "throttling"))) {
				msg = "CPU throttling";
				goto log_info;
			}
			if (strstr(buf, "blocked for more than")) {
				msg = "hung task";
				goto log_info;
			}
			if (strstr(buf, "watchdog") && strstr(buf, "hard LOCKUP")) {
				msg = "hard lockup";
				dump_procs = true;
				goto log_err;
			}
			if (strstr(buf, "soft lockup") && strstr(buf, "stuck")) {
				msg = "soft lockup";
				dump_procs = true;
				goto log_err;
			}
			if (strstr(buf, "watchdog") && strstr(buf, "hard LOCKUP")) {
				msg = "hard lockup";
				dump_procs = true;
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

log_err:
			if (dump_procs || stress_klog_err_no_exceptions(buf)) {
				stress_klog_kernel_cmdline();
				/* rate limit process dumping */
				if ((stress_time_now() - last_logged) > 30.0)
					stress_dump_processes();
				pr_err("%s: %s: %s '%s'\n", name, msg, ts, ptr);
				last_logged = stress_time_now();
				g_shared->klog_errors++;
				continue;
			}
log_info:
			if (stress_klog_err_no_exceptions(buf)) {
				stress_klog_kernel_cmdline();
				pr_inf("%s: %s: %s '%s'\n", name, msg, ts, ptr);
				last_logged = stress_time_now();
			}
		}
		(void)fflush(klog_fp);
		(void)fclose(klog_fp);
		_exit(EXIT_SUCCESS);
	}
	(void)fflush(klog_fp);
	(void)fclose(klog_fp);
#endif
}

/*
 *  stress_klog_stop()
 *	stop klog monitoring child process
 */
void stress_klog_stop(bool *success)
{
#if defined(__linux__)
	if (g_opt_flags & OPT_FLAGS_KLOG_CHECK) {
		if (g_shared->klog_errors) {
			pr_inf("%s: detected %" PRIu64 " kernel error messages\n",
				name, g_shared->klog_errors);
			*success = false;
		}

		if (klog_pid > 1)
			(void)stress_kill_pid_wait(klog_pid, NULL);
		klog_pid = -1;
		g_shared->klog_errors = 0;
	}
#else
	(void)success;
#endif
}
