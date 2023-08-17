// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-sort.h"

#define MAX_STATES 	(64)

void stress_log_cpuidle_info(void)
{
#if defined(__linux__)
	DIR *cpu_dir;
	struct dirent *cpu_d;
	char *states[MAX_STATES];
	char buf[MAX_STATES * 32];
	size_t i, max_states = 0, max_cpus = 0;

	(void)memset(states, 0, sizeof(states));
	cpu_dir = opendir("/sys/devices/system/cpu");
	if (!cpu_dir)
		return;

	/*
	 *  gather all known cpu states for all cpus
	 */
	while ((cpu_d = readdir(cpu_dir)) != NULL) {
		char cpuidle_path[1024];
		DIR *cpuidle_dir;
		struct dirent *cpuidle_d;

		if (strncmp(cpu_d->d_name, "cpu", 3))
			continue;

		(void)snprintf(cpuidle_path, sizeof(cpuidle_path),
			"/sys/devices/system/cpu/%s/cpuidle", cpu_d->d_name);
		cpuidle_dir = opendir(cpuidle_path);
		if (!cpuidle_dir)
			continue;

		max_cpus++;
		while ((cpuidle_d = readdir(cpuidle_dir)) != NULL) {
			char path[PATH_MAX], state[64], *ptr;

			if (strncmp(cpuidle_d->d_name, "state", 5))
				continue;
			(void)snprintf(path, sizeof(path), "%s/%s/name", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, state, sizeof(state)) < 1)
				continue;
			ptr = strchr(state, '\n');
			if (ptr)
				*ptr = '\0';
			for (i = 0; i < max_states; i++) {
				if (strcmp(states[i], state) == 0)
					break;
			}
			if ((i == max_states) && (i < SIZEOF_ARRAY(states))) {
				states[i] = strdup(state);
				if (states[i])
					max_states++;
			}
		}
		(void)closedir(cpuidle_dir);
	}
	(void)closedir(cpu_dir);
	if (max_states > 0) {
		qsort(states, max_states, sizeof(char *), stress_sort_cmp_str);
		(void)memset(buf, 0, sizeof(buf));
		for (i = 0; i < max_states; i++) {
			(void)shim_strlcat(buf, " ", sizeof(buf));
			(void)shim_strlcat(buf, states[i], sizeof(buf));
			free(states[i]);
		}
		pr_dbg("CPU%s %zu idle state%s:%s\n",
			(max_cpus == 1) ? " has" : "s have",
			max_states, (max_states == 1) ? "" : "s", buf);
	}
#endif
}
