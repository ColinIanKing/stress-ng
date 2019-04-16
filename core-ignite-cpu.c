/*
 * Copyright (C) 2016-2019 Canonical, Ltd.
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

typedef struct {
	uint64_t max_freq;		/* Max scaling frequency */
	uint64_t cur_freq;		/* Original scaling frequency */
	char cur_governor[128];		/* Original governor setting */
	bool set_failed;		/* True if we can't set the freq */
} cpu_setting_t;

static cpu_setting_t *cpu_settings;	/* Array of cpu settings */

static pid_t pid;			/* PID of ignite process */
static bool enabled;			/* true if ignite process running */
static uint32_t max_cpus;		/* max cpus configured */

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

static int ignite_cpu_set(
	const uint32_t cpu,
	const uint64_t freq,
	const char *governor)
{
	char path[PATH_MAX];
	int ret1 = 0, ret2 = 0;

	if (freq > 0) {
		char buffer[128];

		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRIu32
			"/cpufreq/scaling_setspeed", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", freq);
		ret1 = system_write(path, buffer, strlen(buffer));
	}

	if (*governor != '\0') {
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRIu32
			"/cpufreq/scaling_governor", cpu);
		ret2 = system_write(path, governor, strlen(governor));
	}

	return ((ret1 < 0) || (ret2 < 0)) ? -1 : 0;
}

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

	max_cpus = stress_get_processors_configured();
	if (max_cpus < 1)
		max_cpus = 1;	/* Has to have at least 1 cpu! */
	cpu_settings = calloc((size_t)max_cpus, sizeof(*cpu_settings));
	if (!cpu_settings) {
		pr_dbg("ignite-cpu: no cpu settings allocated\n");
	} else {
		uint32_t cpu;
		char buffer[128];
		char path[PATH_MAX];

		/*
		 *  Gather per-cpu max scaling frequencies and governors
		 */
		for (cpu = 0; cpu < max_cpus; cpu++) {
			int ret;

			/* Assume failed */
			cpu_settings[cpu].max_freq = 0;
			cpu_settings[cpu].set_failed = true;

			(void)memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_max_freq", cpu);
			ret = system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].max_freq);
				if (ret == 1)
					cpu_settings[cpu].set_failed = false;
			}

			(void)memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_cur_freq", cpu);
			ret = system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].cur_freq);
				if (ret == 1)
					cpu_settings[cpu].set_failed = false;
			}

			(void)memset(cpu_settings[cpu].cur_governor, 0,
				sizeof(cpu_settings[cpu].cur_governor));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_governor", cpu);
			ret = system_read(path, cpu_settings[cpu].cur_governor,
					  sizeof(cpu_settings[cpu].cur_governor));
			(void)ret;
		}
	}

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

		(void)shim_strlcpy(settings[i].setting, buf, len);
		settings[i].setting_len = len;
		settings[i].ignore = false;
		n++;
	}

	if (n == 0)
		return;

	enabled = true;

	pid = fork();
	if (pid < 0) {
		pr_dbg("ignite-cpu: failed to start ignite cpu daemon, "
			"errno=%d (%s)\n", errno, strerror(errno));
		return;
	} else if (pid == 0) {
		/* Child */

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();
		stress_set_proc_name("stress-ng-ignite");

		while (g_keep_stressing_flag) {

			for (i = 0; settings[i].path; i++) {
				if (settings[i].ignore)
					continue;
				(void)system_write(settings[i].path,
					settings[i].default_setting,
					settings[i].default_setting_len);
			}

			if (cpu_settings) {
				uint32_t cpu;

				/*
				 *  Attempt to crank CPUs up to max freq
				 */
				for (cpu = 0; cpu < max_cpus; cpu++) {
					int ret;

					if (cpu_settings[cpu].set_failed)
						continue;

					ret = ignite_cpu_set(cpu,
						cpu_settings[cpu].max_freq,
						"performance");
					if (ret < 0)
						cpu_settings[cpu].set_failed = true;
				}
			}
			(void)sleep(1);
		}
		_exit(0);
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
		(void)shim_waitpid(pid, &status, 0);
	}

	if (cpu_settings) {
		uint32_t cpu;

		for (cpu = 0; cpu < max_cpus; cpu++) {
			ignite_cpu_set(cpu,
				cpu_settings[cpu].cur_freq,
				cpu_settings[cpu].cur_governor);
		}
		free(cpu_settings);
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
