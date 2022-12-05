/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-thermal-zone.h"

#if defined(STRESS_THERMAL_ZONES)

/*
 *  stress_tz_type_instance()
 *	return the number of existing occurrences of
 *	a named type in the tz_info_list.
 */
static uint32_t stress_tz_type_instance(
	stress_tz_info_t *tz_info_list,
	const char *type)
{
	stress_tz_info_t *tz_info;
	uint32_t type_instance = 0;

	if (!type)
		return 0;

	for (tz_info = tz_info_list; tz_info; tz_info = tz_info->next) {
		if (!strcmp(type, tz_info->type))
			type_instance++;
	}
	return type_instance;
}

/*
 *  stress_tz_type_fix()
 *	fix up type name, replace non-alpha/digits with _
 */
static void stress_tz_type_fix(char *type)
{
	char *ptr;

	for (ptr = type; *ptr; ptr++) {
		if (isalnum((int)*ptr))
			continue;
		*ptr = '_';
	}
}

/*
 *  stress_tz_init()
 *	gather all thermal zones
 */
int stress_tz_init(stress_tz_info_t **tz_info_list)
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
		stress_tz_info_t *tz_info;

		/* Ignore non TZ interfaces */
		if (strncmp(entry->d_name, "thermal_zone", 12))
			continue;

		/* Ensure we don't overstep the max limit of TZs */
		if (i >= STRESS_THERMAL_ZONES_MAX)
			break;

		if ((tz_info = calloc(1, sizeof(*tz_info))) == NULL) {
			pr_err("cannot allocate thermal information\n");
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
				stress_tz_type_fix(type);
				tz_info->type = strdup(type);
				tz_info->type_instance = stress_tz_type_instance(*tz_info_list, type);
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
 *  stress_tz_free()
 *	free thermal zones
 */
void stress_tz_free(stress_tz_info_t **tz_info_list)
{
	stress_tz_info_t *tz_info = *tz_info_list;

	while (tz_info) {
		stress_tz_info_t *next = tz_info->next;

		free(tz_info->path);
		free(tz_info->type);
		free(tz_info);
		tz_info = next;
	}
}

/*
 *  stress_tz_get_temperatures()
 *	collect valid thermal_zones details
 */
int stress_tz_get_temperatures(stress_tz_info_t **tz_info_list, stress_tz_t *tz)
{
        stress_tz_info_t *tz_info;

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
 *  stress_tz_compare()
 *	sort on type name and if type names are duplicated on
 *	type_instance value
 */
static int stress_tz_compare(const void *p1, const void *p2)
{
	const stress_tz_info_t *const *tz1 = (const stress_tz_info_t *const *)p1;
	const stress_tz_info_t *const *tz2 = (const stress_tz_info_t *const *)p2;
	int ret;

	ret = strcmp((*tz1)->type, (*tz2)->type);
	if (ret == 0)
		return (int)(*tz1)->type_instance - (int)(*tz2)->type_instance;

	return ret;
}


/*
 *  stress_tz_dump()
 *	dump thermal zone temperatures
 */
void stress_tz_dump(FILE *yaml, stress_stressor_t *stressors_list)
{
	bool no_tz_stats = true;
	stress_stressor_t *ss;

	pr_yaml(yaml, "thermal-zones:\n");

	for (ss = stressors_list; ss; ss = ss->next) {
		stress_tz_info_t *tz_info;
		int32_t  j;
		size_t i, n;
		uint64_t total = 0;
		uint32_t count = 0;
		bool dumped_heading = false;
		stress_tz_info_t **tz_infos;

		/* Find how many items in list */
		for (n = 0, tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next, n++)
			;

		/*
		 *  Allocate array, populate with tz_info and sort
		 */
		tz_infos = calloc(n, sizeof(*tz_infos));
		if (!tz_infos) {
			pr_inf("thermal zones: cannot allocate memory to sort zones\n");
			return;
		}
		for (n = 0, tz_info = g_shared->tz_info; tz_info; tz_info = tz_info->next, n++)
			tz_infos[n] = tz_info;

		qsort(tz_infos, n, sizeof(*tz_infos), stress_tz_compare);

		for (i = 0; i < n; i++) {
			tz_info = tz_infos[i];
			total = 0;
			count = 0;

			for (j = 0; j < ss->started_instances; j++) {
				const uint64_t temp =
					ss->stats[j]->tz.tz_stat[tz_info->index].temperature;
				/* Avoid crazy temperatures. e.g. > 250 C */
				if (temp <= 250000) {
					total += temp;
					count++;
				}
			}

			if (total) {
				const double temp = ((double)total / count) / 1000.0;
				char *munged = stress_munge_underscore(ss->stressor->name);

				if (!dumped_heading) {
					dumped_heading = true;
					pr_inf("%s:\n", munged);
					pr_yaml(yaml, "    - stressor: %s\n",
						munged);
				}

				if (stress_tz_type_instance(g_shared->tz_info, tz_info->type) <= 1) {
					pr_inf("%20s %7.2f C (%.2f K)\n",
						tz_info->type, temp, temp + 273.15);
					pr_yaml(yaml, "      %s: %7.2f\n",
						tz_info->type, temp);
				} else {
					pr_inf("%20s%d %7.2f C (%.2f K)\n",
						tz_info->type,
						tz_info->type_instance,
						temp, temp + 273.15);
					pr_yaml(yaml, "      %s%d: %7.2f\n",
						tz_info->type,
						tz_info->type_instance,
						temp);
				}
				no_tz_stats = false;
			}
		}
		if (total)
			pr_yaml(yaml, "\n");

		free(tz_infos);
	}

	if (no_tz_stats)
		pr_inf("thermal zone temperatures not available\n");
}
#endif
