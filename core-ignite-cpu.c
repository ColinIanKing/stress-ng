/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-arch.h"

#define SETTING_FREQ			(0x01)
#define SETTING_ENERGY_PERF_BIAS	(0x02)
#define SETTING_GOVERNOR		(0x04)

typedef struct {
	const char *path;		/* Path of /sys control */
	const char *default_setting;	/* Default maximizing setting to use */
	size_t default_setting_len;	/* Length of default setting */
	char *setting;			/* Original setting to restore it */
	size_t setting_len;		/* Length of setting */
	bool ignore;			/* true to ignore using this */
} stress_settings_t;

typedef struct {
	uint64_t max_freq;		/* Max scaling frequency */
	uint64_t cur_freq;		/* Original scaling frequency */
	char cur_governor[128];		/* Original governor setting */
	uint8_t setting_flag;		/* 0 if setting can't be read or set */
	int8_t	energy_perf_bias;	/* Energy perf bias */
} stress_cpu_setting_t;

static stress_cpu_setting_t *cpu_settings; /* Array of cpu settings */

static pid_t pid;			/* PID of ignite process */
static bool enabled;			/* true if ignite process running */
static int32_t max_cpus;		/* max cpus configured */

#define SETTING(path, default_setting)	\
	{ path, default_setting, 0, NULL, 0, false }

static stress_settings_t settings[] = {
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	/* x86 Intel P-State maximizing settings */
	SETTING("/sys/devices/system/cpu/intel_pstate/max_perf_pct", "100"),
	SETTING("/sys/devices/system/cpu/intel_pstate/no_turbo", "0"),
#endif
	SETTING(NULL, NULL)
};


static void stress_ignite_cpu_set(
	const int32_t cpu,
	const uint64_t freq,
	const int8_t energy_perf_bias,
	const char *governor,
	uint8_t *setting_flag)
{
	char path[PATH_MAX];

	if ((*setting_flag & SETTING_FREQ) && (freq > 0)) {
		char buffer[128];

		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/cpufreq/scaling_setspeed", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", freq);
		if (system_write(path, buffer, strlen(buffer)) < 0)
			*setting_flag &= ~SETTING_FREQ;
	}
	if ((*setting_flag & SETTING_ENERGY_PERF_BIAS) && (energy_perf_bias >= 0)) {
		char buffer[128];

		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRIu32
			"/power/energy_perf_bias", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRIu8 "\n", energy_perf_bias);
		if (system_write(path, buffer, strlen(buffer)) < 0)
			*setting_flag &= ~SETTING_ENERGY_PERF_BIAS;
	}
	if ((*setting_flag & SETTING_GOVERNOR) && (*governor != '\0')) {
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRIu32
			"/cpufreq/scaling_governor", cpu);
		if (system_write(path, governor, strlen(governor)) < 0)
			*setting_flag &= ~SETTING_GOVERNOR;
	}
}

/*
 *  stress_ignite_cpu_start()
 *	crank up the CPUs, start a child process to continually
 *	set the most demanding CPU settings
 */
void stress_ignite_cpu_start(void)
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
		int32_t cpu;
		char buffer[128];
		char path[PATH_MAX];

		/*
		 *  Gather per-cpu max scaling frequencies and governors
		 */
		for (cpu = 0; cpu < max_cpus; cpu++) {
			ssize_t ret;

			cpu_settings[cpu].max_freq = 0;
			cpu_settings[cpu].setting_flag = 0;
			cpu_settings[cpu].energy_perf_bias = -1;

			(void)memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_max_freq", cpu);
			ret = system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].max_freq);
				if (ret == 1)
					cpu_settings[cpu].setting_flag |= SETTING_FREQ;
			}

			(void)memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_cur_freq", cpu);
			ret = system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].cur_freq);
				if (ret != 1)
					cpu_settings[cpu].setting_flag &= ~SETTING_FREQ;
			}

			(void)memset(cpu_settings[cpu].cur_governor, 0,
				sizeof(cpu_settings[cpu].cur_governor));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/cpufreq/scaling_governor", cpu);
			ret = system_read(path, cpu_settings[cpu].cur_governor,
					  sizeof(cpu_settings[cpu].cur_governor));
			if (ret > 0)
				cpu_settings[cpu].setting_flag |= SETTING_GOVERNOR;

			(void)memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRIu32
				"/power/energy_perf_bias", cpu);
			ret = system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				int8_t bias;

				ret = sscanf(buffer, "%" SCNd8, &bias);
				if (ret == 1) {
					cpu_settings[cpu].energy_perf_bias = bias;
					cpu_settings[cpu].setting_flag |= SETTING_ENERGY_PERF_BIAS;
				}
			}
		}
	}

	pid = -1;
	for (i = 0; settings[i].path; i++) {
		char buf[4096];
		ssize_t ret;
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
				"errno=%zd (%s)\n",
				settings[i].path, settings[i].default_setting,
				-ret, strerror((int)-ret));
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

		while (keep_stressing_flag()) {
			for (i = 0; settings[i].path; i++) {
				if (settings[i].ignore)
					continue;
				(void)system_write(settings[i].path,
					settings[i].default_setting,
					settings[i].default_setting_len);
			}

			if (cpu_settings) {
				int32_t cpu;

				/*
				 *  Attempt to crank CPUs up to max freq
				 */
				for (cpu = 0; cpu < max_cpus; cpu++) {
pr_inf("HERE: %d %d\n", cpu, cpu_settings[cpu].setting_flag);
					if (cpu_settings[cpu].setting_flag == 0)
						continue;

					stress_ignite_cpu_set(cpu,
						cpu_settings[cpu].max_freq,
						0,
						"performance",
						&cpu_settings[cpu].setting_flag);
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
 *  stress_ignite_cpu_stop()
 *	stop updating settings and restore to original settings
 */
void stress_ignite_cpu_stop(void)
{
	size_t i;
	int status;

	if (pid > -1) {
		(void)kill(pid, SIGTERM);
		(void)kill(pid, SIGKILL);
		(void)shim_waitpid(pid, &status, 0);
	}

	if (cpu_settings) {
		int32_t cpu;

		for (cpu = 0; cpu < max_cpus; cpu++) {
			stress_ignite_cpu_set(cpu,
				cpu_settings[cpu].cur_freq,
				cpu_settings[cpu].energy_perf_bias,
				cpu_settings[cpu].cur_governor,
				&cpu_settings[cpu].setting_flag);
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
