/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-cpuidle.h"
#include "core-sort.h"

#include <ctype.h>

static cpu_cstate_t *cpu_cstate_list;
static size_t cpu_cstate_list_len;

#if defined(STRESS_ARCH_X86)
static const char * const busy_state = "C0";
#else
static const char * const busy_state = "BUSY";
#endif

/*
 *  stress_cpuidle_cstate_list_head()
 *	return head of C-state list
 */
cpu_cstate_t *stress_cpuidle_cstate_list_head(void)
{
	return cpu_cstate_list;
}

#if defined(__linux__)

static CONST int stress_cpuidle_value(const char *cstate)
{
	int val;

	while (isalpha((unsigned char)*cstate))
		cstate++;
	if (sscanf(cstate , "%d", &val) == 1)
		return val;
	return 0;
}

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
		int cmp;

		if (*cstate == 'C')
			cmp = stress_cpuidle_value(cstate) - stress_cpuidle_value((*cc)->cstate);
		else
			cmp = 0;

		/* Same C number, compare strings? */
		if (cmp == 0)
			cmp = strcmp(cstate, (*cc)->cstate);
		/* Identical C-state, not unique, don't add */
		if (cmp == 0)
			return;
		if (cmp < 0)
			break;
	}
	new_cc = (cpu_cstate_t *)malloc(sizeof(*new_cc));
	if (UNLIKELY(!new_cc))
		return;
	new_cc->cstate = shim_strdup(cstate);
	if (UNLIKELY(!new_cc->cstate)) {
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
	bool has_c0 = false;

	cpu_cstate_list = NULL;
	cpu_cstate_list_len = 0;

	cpu_dir = opendir("/sys/devices/system/cpu");
	if (UNLIKELY(!cpu_dir))
		return;

	/*
	 *  gather all known cpu states for all cpus
	 */
	while ((cpu_d = readdir(cpu_dir)) != NULL) {
		char cpuidle_path[512];
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
			char path[PATH_MAX + 512], data[64], *ptr;
			uint32_t residency = 0;

			if (strncmp(cpuidle_d->d_name, "state", 5))
				continue;
			(void)snprintf(path, sizeof(path), "%s/%s/residency", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, data, sizeof(data)) > 0) {
				if (sscanf(data, "%" SCNu32, &residency) != 1)
					continue;
			}
			(void)snprintf(path, sizeof(path), "%s/%s/name", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, data, sizeof(data)) < 1)
				continue;
			ptr = strchr(data, '\n');
			if (ptr)
				*ptr = '\0';

			if (strcmp(data, "C0") == 0)
				has_c0 = true;

			stress_cpuidle_cstate_add_unique(data, residency);
		}
		(void)closedir(cpuidle_dir);
	}
	(void)closedir(cpu_dir);

	if (cpu_cstate_list && !has_c0)
		stress_cpuidle_cstate_add_unique(busy_state, 0);
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

static void stress_cpuidle_read_cstates(
	const int which,
	stress_cstate_stats_t *cstate_stats)
{
	DIR *cpu_dir;
	const struct dirent *cpu_d;
	cpu_cstate_t *cc;
	size_t i;
	stress_cstate_stats_t stats;

	if (UNLIKELY((which < 0) || (which > 1)))
		return;

	cpu_dir = opendir("/sys/devices/system/cpu");
	if (UNLIKELY(!cpu_dir))
		return;

	for (i = 0; i < STRESS_CSTATES_MAX; i++) {
		stats.valid = false;
		stats.time[i] = 0.0;
		stats.residency[i] = 0.0;
	}

	/*
	 *  total up cpu C state timings
	 */
	while ((cpu_d = readdir(cpu_dir)) != NULL) {
		char cpuidle_path[768];
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
			char path[PATH_MAX + 768], cstate[64], data[64], *ptr;
			uint64_t cstate_time;
			double now;

			if (strncmp(cpuidle_d->d_name, "state", 5))
				continue;

			(void)snprintf(path, sizeof(path), "%s/%s/name", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, cstate, sizeof(cstate)) < 1)
				continue;
			ptr = strchr(cstate, '\n');
			if (ptr)
				*ptr = '\0';

			(void)snprintf(path, sizeof(path), "%s/%s/time", cpuidle_path, cpuidle_d->d_name);
			if (stress_system_read(path, data, sizeof(data)) < 1)
				continue;
			now = stress_time_now();
			cstate_time = 0;
			if (sscanf(data, "%" SCNu64, &cstate_time) != 1)
				continue;
			for (i = 0, cc = cpu_cstate_list; (i < STRESS_CSTATES_MAX) && cc; i++, cc = cc->next) {
				if (strcmp(cc->cstate, cstate) == 0) {
					stats.time[i] += now;
					stats.residency[i] += (double)cstate_time;
					stats.valid = true;
					break;
				}
			}
		}
		(void)closedir(cpuidle_dir);
	}
	(void)closedir(cpu_dir);

	if (which == 0) {
		shim_memcpy(cstate_stats, &stats, sizeof(*cstate_stats));
	} else {
		for (i = 0, cc = cpu_cstate_list; (i < STRESS_CSTATES_MAX) && cc; i++, cc = cc->next) {
			cstate_stats->valid = stats.valid;
			cstate_stats->time[i] = stats.time[i] - cstate_stats->time[i];
			cstate_stats->residency[i] = stats.residency[i] - cstate_stats->residency[i];
		}
	}
}

void stress_cpuidle_read_cstates_begin(stress_cstate_stats_t *cstate_stats)
{
	stress_cpuidle_read_cstates(0, cstate_stats);
}

void stress_cpuidle_read_cstates_end(stress_cstate_stats_t *cstate_stats)
{
	stress_cpuidle_read_cstates(1, cstate_stats);
}

void stress_cpuidle_dump(FILE *yaml, stress_stressor_t *stressors_list)
{
	stress_stressor_t *ss;

	pr_yaml(yaml, "C-states:\n");

	for (ss = stressors_list; ss; ss = ss->next) {
		size_t i;
		int32_t j;
		double residencies[STRESS_CSTATES_MAX];
		cpu_cstate_t *cc;
		double c0_residency = 100.0;
		bool valid = false;

		if (ss->ignore.run)
			continue;

		for (i = 0, cc = cpu_cstate_list; (i < STRESS_CSTATES_MAX) && cc; i++, cc = cc->next) {
			double duration_us = 0.0;
			double residency_us = 0.0;

			for (j = 0; j < ss->instances; j++) {
				duration_us += ss->stats[j]->cstates.time[i];
				residency_us += ss->stats[j]->cstates.residency[i];
				valid |= ss->stats[j]->cstates.valid;

			}
			residencies[i] = (duration_us > 0) ?
					100.0 * residency_us / (1000000.0 * duration_us) : 0.0;
			c0_residency -= residencies[i];
		}
		/* and zero residuals to be safe */
		for (; i < STRESS_CSTATES_MAX; i++)
			 residencies[i] = 0.0;

		if (valid) {
			pr_inf("%s:\n", ss->stressor->name);
			pr_yaml(yaml, "    - stressor: %s\n", ss->stressor->name);

			for (i = 0, cc = cpu_cstate_list; (i < STRESS_CSTATES_MAX) && cc; i++, cc = cc->next) {
				if (strcmp(cc->cstate, busy_state) == 0)
					residencies[i] = c0_residency;
				pr_inf(" %-5.5s %6.2f%%\n", cc->cstate, residencies[i]);
				pr_yaml(yaml, "      %s: %.2f\n", cc->cstate, residencies[i]);
			}
			pr_yaml(yaml, "\n");
		}
	}
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
	buf = (char *)calloc(len, sizeof(*buf));
	if (UNLIKELY(!buf))
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
