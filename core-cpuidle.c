/*
 * Copyright (C) 2023-2024 Colin Ian King.
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
#include "core-cpuidle.h"
#include "core-sort.h"

static cpu_cstate_t *cpu_cstate_list;
static size_t cpu_cstate_list_len;

/*
 *  stress_cpuidle_cstate_list_head()
 *	return head of C-state list
 */
cpu_cstate_t *stress_cpuidle_cstate_list_head(void)
{
	return cpu_cstate_list;
}

#if defined(__linux__)
/*
 *  stress_cpuidle_cstate_add_unique()
 *	add a new unique C-state to the C-state list
 */
static void stress_cpuidle_cstate_add_unique(
	const char *cstate,
	const uint32_t residency)
{
	cpu_cstate_t **cc;
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
#endif

/*
 *  stress_cpuidle_init()
 *	initialize the C-state CPU idle list
 */
void stress_cpuidle_init(void)
{
#if defined(__linux__)
	DIR *cpu_dir;
	const struct dirent *cpu_d;

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
		const struct dirent *cpuidle_d;

		if (strncmp(cpu_d->d_name, "cpu", 3))
			continue;

		(void)snprintf(cpuidle_path, sizeof(cpuidle_path),
			"/sys/devices/system/cpu/%s/cpuidle", cpu_d->d_name);
		cpuidle_dir = opendir(cpuidle_path);
		if (!cpuidle_dir)
			continue;

		while ((cpuidle_d = readdir(cpuidle_dir)) != NULL) {
			char path[PATH_MAX + 32], data[64], *ptr;
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

/*
 *  stress_cpuidle_free()
 *	free the C-state CPU idle list
 */
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

/*
 *  stress_cpuidle_log_info()
 *	log the C-states, only log them if list contains C-states
 */
void stress_cpuidle_log_info(void)
{
	char *buf;
	cpu_cstate_t *cc;
	size_t len = 1;

	if (cpu_cstate_list_len < 1)
		return;

	for (cc = cpu_cstate_list; cc; cc = cc->next) {
		len += strlen(cc->cstate) + 2;
	}
	buf = calloc(len, sizeof(*buf));
	if (!buf)
		return;

	for (cc = cpu_cstate_list; cc; cc = cc->next) {
		(void)shim_strlcat(buf, cc->cstate, len);
		if (cc->next)
			(void)shim_strlcat(buf, ", ", len);
	}
	pr_dbg("CPU%s %zu idle state%s: %s\n",
		(cpu_cstate_list_len == 1) ? " has" : "s have",
		cpu_cstate_list_len, (cpu_cstate_list_len == 1) ? "" : "s", buf);
	free(buf);
}
