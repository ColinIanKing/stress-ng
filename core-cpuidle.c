// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-cpuidle.h"
#include "core-sort.h"

static cpu_cstate_t *cpu_cstate_list;
static size_t cpu_cstate_list_len;

cpu_cstate_t *stress_cpuidle_cstate_list_head(void)
{
	return cpu_cstate_list;
}

static void stress_cpuidle_cstate_add_unique(
	const char *cstate,
	const uint32_t residency)
{
	cpu_cstate_t **cc = &cpu_cstate_list;
	cpu_cstate_t *new_cc;

	for (cc = &cpu_cstate_list; *cc; cc = &(*cc)->next) {
		const int cmp = strcmp(cstate, (*cc)->cstate);

		if (cmp == 0)
			return;
		if (cmp < 0)
			break;
	}
	new_cc = malloc(sizeof(*new_cc));
	if (!new_cc)
		return;
	new_cc->cstate = strdup(cstate);
	if (!new_cc->cstate) {
		free(new_cc);
		return;
	}
	new_cc->residency = residency;
	new_cc->next = *cc;
	*cc = new_cc;
	cpu_cstate_list_len++;
}

void stress_cpuidle_init(void)
{
#if defined(__linux__)
	DIR *cpu_dir;
	struct dirent *cpu_d;
	size_t max_cpus = 0;

	cpu_cstate_list = NULL;
	cpu_cstate_list_len = 0;

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
			char path[PATH_MAX], data[64], *ptr;
			uint32_t residency = 0;

			if (strncmp(cpuidle_d->d_name, "state", 5))
				continue;
			(void)snprintf(path, sizeof(path), "%s/%s/residency", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, data, sizeof(data)) > 0)
				residency = (uint32_t)atoi(data);
			(void)snprintf(path, sizeof(path), "%s/%s/name", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, data, sizeof(data)) < 1)
				continue;
			ptr = strchr(data, '\n');
			if (ptr)
				*ptr = '\0';

			stress_cpuidle_cstate_add_unique(data, residency);
		}
		(void)closedir(cpuidle_dir);
	}
	(void)closedir(cpu_dir);
#else
	cpu_cstate_list = NULL;
	cpu_cstate_list_len = 0;
#endif
}

void stress_cpuidle_free(void)
{
	cpu_cstate_t *cc = cpu_cstate_list;

	while (cc) {
		cpu_cstate_t *next = cc->next;

		free(cc->cstate);
		free(cc);
		cc = next;
	}
	cpu_cstate_list = NULL;
	cpu_cstate_list_len = 0;
}

void stress_cpuidle_log_info(void)
{
	char *buf;
	cpu_cstate_t *cc;
	size_t len = 1;

	if (cpu_cstate_list_len < 1)
		return;

	for (cc = cpu_cstate_list; cc; cc = cc->next) {
		len += strlen(cc->cstate) + 1;
	}
	buf = calloc(len, sizeof(*buf));
	if (!buf)
		return;

	for (cc = cpu_cstate_list; cc; cc = cc->next) {
		(void)shim_strlcat(buf, " ", len);
		(void)shim_strlcat(buf, cc->cstate, len);
	}
	pr_dbg("CPU%s %zu idle state%s:%s\n",
		(cpu_cstate_list_len == 1) ? " has" : "s have",
		cpu_cstate_list_len, (cpu_cstate_list_len == 1) ? "" : "s", buf);
	free(buf);
}
