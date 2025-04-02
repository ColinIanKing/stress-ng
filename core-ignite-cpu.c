/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-arch.h"
#include "core-builtin.h"
#include "core-ignite-cpu.h"
#include "core-killpid.h"

#define SETTING_SCALING_FREQ		(0x01)
#define SETTING_CPUINFO_FREQ		(0x02)
#define SETTING_FREQ			(SETTING_SCALING_FREQ | SETTING_CPUINFO_FREQ)
#define SETTING_ENERGY_PERF_BIAS	(0x04)
#define SETTING_GOVERNOR		(0x08)
#define SETTING_RESUME_LATENCY_US	(0x10)

typedef struct {
	const char *path;		/* Path of /sys control */
	const char *default_setting;	/* Default maximizing setting to use */
	size_t default_setting_len;	/* Length of default setting */
	char *setting;			/* Original setting to restore it */
	size_t setting_len;		/* Length of setting */
	bool ignore;			/* true to ignore using this */
} stress_settings_t;

typedef struct {
	uint64_t scaling_max_freq;	/* Max scaling frequency */
	uint64_t scaling_min_freq;	/* Min scaling frequency */
	uint64_t cpuinfo_max_freq;	/* Max cpu frequency */
	uint64_t cpuinfo_min_freq;	/* Min cpu frequency */
	uint64_t resume_latency_us;	/* pm_qos_resume_latency_us */
	char cur_governor[128];		/* Original governor setting */
	uint8_t setting_flag;		/* 0 if setting can't be read or set */
	int8_t	energy_perf_bias;	/* Energy perf bias */
} stress_cpu_setting_t;

static stress_cpu_setting_t *cpu_settings; /* Array of cpu settings */

static pid_t pid;			/* PID of ignite process */
static bool enabled;			/* true if ignite process running */
static int32_t max_cpus;		/* max cpus configured */
static int latency_fd;			/* /dev/cpu_dma_latency fd */

#define SETTING(path, default_setting)	\
	{ path, default_setting, 0, NULL, 0, false }

static stress_settings_t settings[] = {
#if defined(__linux__) &&	\
    defined(STRESS_ARCH_X86)
	/* x86 Intel P-State maximizing settings */
	SETTING("/sys/devices/system/cpu/intel_pstate/max_perf_pct", "100"),
	SETTING("/sys/devices/system/cpu/intel_pstate/no_turbo", "0"),
	SETTING("/sys/devices/system/cpu/cpufreq/boost", "1"),
#endif
	SETTING(NULL, NULL)
};

/*
 *  stress_ignite_cpu_set()
 *	attempt to apply settings, disable settings in settings_flag
 *	if we cannot apply them.
 */
static void stress_ignite_cpu_set(
	bool maximize_freq,
	const int32_t cpu,
	const uint64_t max_freq,
	const uint64_t min_freq,
	const uint64_t resume_latency_us,
	const int8_t energy_perf_bias,
	const char *governor,
	uint8_t *setting_flag)
{
	char path[PATH_MAX];
	char buffer[128];
	const int max_retries = 16;

	if (((*setting_flag & SETTING_FREQ) == SETTING_FREQ) &&
		(min_freq > 0) && (max_freq > 0) && (min_freq < max_freq)) {
		const int64_t freq_delta = (max_freq - min_freq) / 10;
		uint64_t freq;
		int retry = 0;

		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/cpufreq/scaling_max_freq", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", max_freq);
		if (stress_system_write(path, buffer, strlen(buffer)) < 0)
			*setting_flag &= ~SETTING_SCALING_FREQ;

		/* Try to set min to be 100% of max down to lowest, which ever works first */
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/cpufreq/scaling_min_freq", cpu);
		freq = (maximize_freq) ? max_freq : min_freq;
		while ((retry++ < max_retries) && (freq_delta > 0) && (freq >= min_freq)) {
			(void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", freq);
			if (stress_system_write(path, buffer, strlen(buffer)) >= 0)
				break;
			freq -= freq_delta;
		}
	}

	if (*setting_flag & SETTING_RESUME_LATENCY_US) {
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/power/pm_qos_resume_latency_us", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRIu64 "\n", resume_latency_us);
		if (stress_system_write(path, buffer, strlen(buffer)) < 0)
			*setting_flag &= ~SETTING_RESUME_LATENCY_US;
	}

	if ((*setting_flag & SETTING_ENERGY_PERF_BIAS) && (energy_perf_bias >= 0)) {
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/power/energy_perf_bias", cpu);
		(void)snprintf(buffer, sizeof(buffer), "%" PRId8 "\n", energy_perf_bias);
		if (stress_system_write(path, buffer, strlen(buffer)) < 0)
			*setting_flag &= ~SETTING_ENERGY_PERF_BIAS;
	}

	if ((*setting_flag & SETTING_GOVERNOR) && (*governor != '\0')) {
		(void)snprintf(path, sizeof(path),
			"/sys/devices/system/cpu/cpu%" PRId32
			"/cpufreq/scaling_governor", cpu);
		if (stress_system_write(path, governor, strlen(governor)) < 0)
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

	latency_fd = open("/dev/cpu_dma_latency", O_WRONLY);
	if (latency_fd != -1) {
		int32_t lat = 0;

		VOID_RET(ssize_t, write(latency_fd, &lat, sizeof(lat)));
	}

	max_cpus = stress_get_processors_configured();
	if (max_cpus < 1)
		max_cpus = 1;	/* Has to have at least 1 cpu! */
	cpu_settings = (stress_cpu_setting_t *)calloc((size_t)max_cpus, sizeof(*cpu_settings));
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

			cpu_settings[cpu].scaling_max_freq = 0;
			cpu_settings[cpu].scaling_min_freq = 0;
			cpu_settings[cpu].cpuinfo_max_freq = 0;
			cpu_settings[cpu].cpuinfo_min_freq = 0;
			cpu_settings[cpu].resume_latency_us = 0;
			cpu_settings[cpu].setting_flag = 0;
			cpu_settings[cpu].energy_perf_bias = -1;

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/cpufreq/scaling_max_freq", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].scaling_max_freq);
				if (ret == 1)
					cpu_settings[cpu].setting_flag |= SETTING_SCALING_FREQ;
			}

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/cpufreq/scaling_min_freq", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].scaling_min_freq);
				if (ret != 1)
					cpu_settings[cpu].setting_flag &= ~SETTING_SCALING_FREQ;
			}

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/cpufreq/cpuinfo_max_freq", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].cpuinfo_max_freq);
				if (ret == 1)
					cpu_settings[cpu].setting_flag |= SETTING_CPUINFO_FREQ;
			}

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/cpufreq/cpuinfo_min_freq", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].cpuinfo_min_freq);
				if (ret != 1)
					cpu_settings[cpu].setting_flag &= ~SETTING_CPUINFO_FREQ;
			}

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/power/pm_qos_resume_latency_us", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
			if (ret > 0) {
				ret = sscanf(buffer, "%" SCNu64,
					&cpu_settings[cpu].resume_latency_us);
				if (ret == 1)
					cpu_settings[cpu].setting_flag |= SETTING_RESUME_LATENCY_US;
			}

			(void)shim_memset(cpu_settings[cpu].cur_governor, 0,
				sizeof(cpu_settings[cpu].cur_governor));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/cpufreq/scaling_governor", cpu);
			ret = stress_system_read(path, cpu_settings[cpu].cur_governor,
					  sizeof(cpu_settings[cpu].cur_governor));
			if (ret > 0)
				cpu_settings[cpu].setting_flag |= SETTING_GOVERNOR;

			(void)shim_memset(buffer, 0, sizeof(buffer));
			(void)snprintf(path, sizeof(path),
				"/sys/devices/system/cpu/cpu%" PRId32
				"/power/energy_perf_bias", cpu);
			ret = stress_system_read(path, buffer, sizeof(buffer));
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
		ret = stress_system_read(settings[i].path, buf, sizeof(buf) - 1);
		if (ret < 0)
			continue;
		buf[ret] = '\0';
		len = strlen(buf);
		if (len == 0)
			continue;

		settings[i].default_setting_len =
			strlen(settings[i].default_setting);
		/* If we can't update the setting, skip it */
		ret = stress_system_write(settings[i].path,
			settings[i].default_setting,
			settings[i].default_setting_len);
		if (ret < 0) {
			pr_dbg("ignite-cpu: cannot set %s to %s, "
				"errno=%zd (%s)\n",
				settings[i].path, settings[i].default_setting,
				-ret, strerror((int)-ret));
			continue;
		}

		settings[i].setting = (char *)calloc(1, len + 1);
		if (!settings[i].setting)
			continue;

		(void)shim_strscpy(settings[i].setting, buf, len);
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

		stress_parent_died_alarm();
		stress_set_proc_state_str("ignite","periodic");

		while (stress_continue_flag()) {
			for (i = 0; settings[i].path; i++) {
				if (settings[i].ignore)
					continue;
				(void)stress_system_write(settings[i].path,
					settings[i].default_setting,
					settings[i].default_setting_len);
			}

			if (cpu_settings) {
				int32_t cpu;
				const char *governor = (stress_mwc8() < 240) ? "performance" : "powersave";

				/*
				 *  Attempt to crank CPUs up to max freq
				 */
				for (cpu = 0; cpu < max_cpus; cpu++) {
					if (cpu_settings[cpu].setting_flag == 0)
						continue;

					stress_ignite_cpu_set(true, cpu,
						cpu_settings[cpu].cpuinfo_max_freq,
						cpu_settings[cpu].cpuinfo_min_freq,
						1,
						0,
						governor,
						&cpu_settings[cpu].setting_flag);
				}
			}
			(void)sleep(1);
		}
		_exit(0);
	}
}

/*
 *  stress_ignite_cpu_stop()
 *	stop updating settings and restore to original settings
 */
void stress_ignite_cpu_stop(void)
{
	size_t i;

	if (latency_fd != -1) {
		(void)close(latency_fd);
		latency_fd = -1;
	}
	if (pid > -1)
		(void)stress_kill_pid_wait(pid, NULL);

	if (cpu_settings) {
		int32_t cpu;

		for (cpu = 0; cpu < max_cpus; cpu++) {
			stress_ignite_cpu_set(false, cpu,
				cpu_settings[cpu].cpuinfo_max_freq,
				cpu_settings[cpu].cpuinfo_min_freq,
				cpu_settings[cpu].resume_latency_us,
				cpu_settings[cpu].energy_perf_bias,
				cpu_settings[cpu].cur_governor,
				&cpu_settings[cpu].setting_flag);
		}
		free(cpu_settings);
	}

	for (i = 0; settings[i].path; i++) {
		if (settings[i].ignore)
			continue;

		(void)stress_system_write(settings[i].path, settings[i].setting,
			settings[i].setting_len);
		free(settings[i].setting);
		settings[i].setting = NULL;
		settings[i].ignore = true;
	}
	enabled = false;
}
