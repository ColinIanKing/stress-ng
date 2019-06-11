/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

#if defined(STRESS_THERMAL_ZONES)
/*
 *  tz_init()
 *	gather all thermal zones
 */
int tz_init(tz_info_t **tz_info_list)
{
	DIR *dir;
        struct dirent *entry;
	size_t i = 0;

	dir = opendir("/sys/class/thermal");
	if (!dir)
		return 0;

	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		FILE *fp;
		tz_info_t *tz_info;

		/* Ignore non TZ interfaces */
		if (strncmp(entry->d_name, "thermal_zone", 12))
			continue;

		/* Ensure we don't overstep the max limit of TZs */
		if (i >= STRESS_THERMAL_ZONES_MAX)
			break;

		if ((tz_info = calloc(1, sizeof(*tz_info))) == NULL) {
			pr_err("Cannot allocate thermal information\n");
			(void)closedir(dir);
			return -1;
		}
		(void)snprintf(path, sizeof(path),
			"/sys/class/thermal/%s/type",
			entry->d_name);

		tz_info->path = strdup(entry->d_name);
		if (!tz_info->path) {
			free(tz_info);
			(void)closedir(dir);
			return -1;
		}
		tz_info->type = NULL;
		if ((fp = fopen(path, "r")) != NULL) {
			char type[128];

			if (fgets(type, sizeof(type), fp) != NULL) {
				type[strcspn(type, "\n")] = '\0';
				tz_info->type  = strdup(type);
			}
			(void)fclose(fp);
		}
		if (!tz_info->type) {
			free(tz_info->path);
			free(tz_info);
			(void)closedir(dir);
			return -1;
		}
		tz_info->index = i++;
		tz_info->next = *tz_info_list;
		*tz_info_list = tz_info;
	}

	(void)closedir(dir);
	return 0;
}

/*
 *  tz_free()
 *	free thermal zones
 */
void tz_free(tz_info_t **tz_info_list)
{
	tz_info_t *tz_info = *tz_info_list;

	while (tz_info) {
		tz_info_t *next = tz_info->next;

		free(tz_info->path);
		free(tz_info->type);
		free(tz_info);
		tz_info = next;
	}
}

/*
 *  tz_get_temperatures()
 *	collect valid thermal_zones details
 */
int tz_get_temperatures(tz_info_t **tz_info_list, stress_tz_t *tz)
{
        tz_info_t *tz_info;

	for (tz_info = *tz_info_list; tz_info; tz_info = tz_info->next) {
		char path[PATH_MAX];
		FILE *fp;
		const size_t i = tz_info->index;

		(void)snprintf(path, sizeof(path),
			"/sys/class/thermal/%s/temp",
			tz_info->path);

		tz->tz_stat[i].temperature = 0;
		if ((fp = fopen(path, "r")) != NULL) {
			if (fscanf(fp, "%" SCNu64,
			     &tz->tz_stat[i].temperature) != 1) {
				tz->tz_stat[i].temperature = 0;
			}
			(void)fclose(fp);
		}
	}
	return 0;
}

/*
 *  tz_dump()
 *	dump thermal zone temperatures
 */
void tz_dump(FILE *yaml, proc_info_t *procs_head)
{
	bool no_tz_stats = true;
	proc_info_t *pi;

	pr_yaml(yaml, "thermal-zones:\n");

	for (pi = procs_head; pi; pi = pi->next) {
		tz_info_t *tz_info;
		int32_t  j;
		uint64_t total = 0;
		uint32_t count = 0;
		bool dumped_heading = false;

		for (tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next) {
			for (j = 0; j < pi->started_procs; j++) {
				const uint64_t temp = 
					pi->stats[j]->tz.tz_stat[tz_info->index].temperature;
				/* Avoid crazy temperatures. e.g. > 250 C */
				if (temp <= 250000) {
					total += temp;
					count++;
				}
			}

			if (total) {
				const double temp = ((double)total / count) / 1000.0;
				char *munged = stress_munge_underscore(pi->stressor->name);

				if (!dumped_heading) {
					dumped_heading = true;
					pr_inf("%s:\n", munged);
					pr_yaml(yaml, "    - stressor: %s\n",
						munged);
				}
				pr_inf("%20s %7.2f C (%.2f K)\n",
					tz_info->type, temp, temp + 273.15);
				pr_yaml(yaml, "      %s: %7.2f\n",
					tz_info->type, temp);
				no_tz_stats = false;
			}
		}
		if (total)
			pr_yaml(yaml, "\n");
	}

	if (no_tz_stats)
		pr_inf("thermal zone temperatures not available\n");
}

#endif
