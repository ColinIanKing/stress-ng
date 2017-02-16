/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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

typedef struct {
	const char *path;		/* Path of /sys control */
	const char *default_setting;	/* Default maximizing setting to use */
	size_t default_setting_len;	/* Length of default setting */
	char *setting;			/* Original setting to restore it */
	size_t setting_len;		/* Length of setting */
	bool ignore;			/* true to ignore using this */
} settings_t;

static pid_t pid;
static bool enabled;

#define SETTING(path, default_setting)	\
	{ path, default_setting, 0, NULL, 0, false }

static settings_t settings[] = {
#if defined(__linux__) && defined(STRESS_X86)
	/* x86 Intel P-State maximizing settings */
	SETTING("/sys/devices/system/cpu/intel_pstate/max_perf_pct", "100"),
	SETTING("/sys/devices/system/cpu/intel_pstate/no_turbo", "0"),
#endif
	SETTING(NULL, NULL)
};

/*
 *  ignite_cpu_start()
 *	crank up the CPUs, start a child process to continually
 *	set the most demanding CPU settings
 */
void ignite_cpu_start(void)
{
	size_t i, n = 0;

	if (enabled)
		return;

	pid = -1;
	for (i = 0; settings[i].path; i++) {
		char buf[4096];
		int ret;
		size_t len;

		settings[i].ignore = true;
		ret = system_read(settings[i].path, buf, sizeof(buf) - 1);
		if (ret < 0)
			continue;
		buf[ret] = '\0';
		len = strlen(buf);
		if (len == 0)
			continue;

		settings[i].default_setting_len =
			strlen(settings[i].default_setting);
		/* If we can't update the setting, skip it */
		ret = system_write(settings[i].path,
			settings[i].default_setting,
			settings[i].default_setting_len);
		if (ret < 0) {
			pr_dbg("ignite-cpu: cannot set %s to %s, "
				"errno=%d (%s)\n",
				settings[i].path, settings[i].default_setting,
				-ret, strerror(-ret));
			continue;
		}

		settings[i].setting = calloc(1, len + 1);
		if (!settings[i].setting)
			continue;

		strncpy(settings[i].setting, buf, len);
		settings[i].setting_len = len;
		settings[i].ignore = false;
		n++;
	}

	if (n == 0)
		return;

	enabled = true;

	pid = fork();
	if (pid < 0) {
		pr_dbg("failed to start ignite cpu daemon, "
			"errno=%d (%s)\n", errno, strerror(errno));
		return;
	} else if (pid == 0) {
		/* Child */
		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		for (;;) {
			for (i = 0; settings[i].path; i++) {
				if (settings[i].ignore)
					continue;
				(void)system_write(settings[i].path,
					settings[i].default_setting,
					settings[i].default_setting_len);
			}
			sleep(1);
		}
	} else {
		/* Parent */
		(void)setpgid(pid, g_pgrp);
	}
}

/*
 *  ignite_cpu_stop()
 *	stop updating settings and restore to original settings
 */
void ignite_cpu_stop(void)
{
	size_t i;
	int status;

	if (pid > -1) {
		(void)kill(pid, SIGTERM);
		(void)kill(pid, SIGKILL);
		(void)waitpid(pid, &status, 0);
	}

	for (i = 0; settings[i].path; i++) {
		if (settings[i].ignore)
			continue;

		(void)system_write(settings[i].path, settings[i].setting,
			settings[i].setting_len);
		free(settings[i].setting);
		settings[i].setting = NULL;
		settings[i].ignore = true;
	}
	enabled = false;
}
