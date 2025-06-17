/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-rapl.h"

#if defined(STRESS_RAPL)

#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

#include <sys/time.h>
#include <fcntl.h>

/*
 *  stress_rapl_free_domains()
 *	free RAPL domain list
 */
void stress_rapl_free_domains(stress_rapl_domain_t *rapl_domains)
{
	stress_rapl_domain_t *rapl_domain = rapl_domains;

	while (rapl_domain) {
		stress_rapl_domain_t *next = rapl_domain->next;

		free(rapl_domain->name);
		free(rapl_domain->domain_name);
		free(rapl_domain);
		rapl_domain = next;
	}
}

/*
 *  stress_rapl_domain_unique()
 *	returns true if domain_name is not in rapl_domains
 */
static bool stress_rapl_domain_unique(stress_rapl_domain_t *rapl_domains, const char *domain_name)
{
	stress_rapl_domain_t *rapl_domain;

	for (rapl_domain = rapl_domains; rapl_domain; rapl_domain = rapl_domain->next) {
		if (!strcmp(rapl_domain->domain_name, domain_name))
			return false;
	}
	return true;
}

/*
 *  stress_rapl_add_list()
 *	add domain to rapl domain list in domain name sorted order
 */
static void stress_rapl_add_list(stress_rapl_domain_t **rapl_domains, stress_rapl_domain_t *rapl_domain)
{
	stress_rapl_domain_t **l;

	for (l = rapl_domains; *l; l = &(*l)->next) {
		if (strcmp((*l)->domain_name, rapl_domain->domain_name) > 0) {
			rapl_domain->next = *l;
			break;
		}
	}
	*l = rapl_domain;
}

/*
 *  stress_rapl_get_domains()
 */
int stress_rapl_get_domains(stress_rapl_domain_t **rapl_domains)
{
	DIR *dir;
        const struct dirent *entry;
	int n = 0;
	bool unreadable_energy_uj = true;

	dir = opendir("/sys/class/powercap");
	if (dir == NULL) {
		pr_inf("device does not have RAPL, cannot measure power usage, errno=%d (%s)\n",
			errno, strerror(errno));
		return -1;
	}

	while ((entry = readdir(dir)) != NULL) {
		char path[PATH_MAX];
		FILE *fp;
		stress_rapl_domain_t *rapl_domain;
		double ujoules;

		/* Ignore non Intel RAPL interfaces */
		if (strncmp(entry->d_name, "intel-rapl", 10))
			continue;

		/* Check if energy_uj is readable */
		(void)snprintf(path, sizeof(path),
			"/sys/class/powercap/%s/energy_uj",
			entry->d_name);
		if ((fp = fopen(path, "r")) == NULL)
			continue;
		if (fscanf(fp, "%lf\n", &ujoules) != 1) {
			(void)fclose(fp);
			continue;
		}
		(void)fclose(fp);
		unreadable_energy_uj = false;

		if ((rapl_domain = (stress_rapl_domain_t *)calloc(1, sizeof(*rapl_domain))) == NULL) {
			pr_inf("cannot allocate RAPL domain information\n");
			(void)closedir(dir);
			return -1;
		}
		if ((rapl_domain->name = shim_strdup(entry->d_name)) == NULL) {
			pr_inf("cannot allocate RAPL name information\n");
			(void)closedir(dir);
			free(rapl_domain);
			return -1;
		}
		(void)snprintf(path, sizeof(path),
			"/sys/class/powercap/%s/max_energy_range_uj",
			entry->d_name);

		rapl_domain->max_energy_uj = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_RAPLSTAT].time = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_STRESSOR].time = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_RAPLSTAT].power_watts = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_STRESSOR].power_watts = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_RAPLSTAT].energy_uj = 0.0;
		rapl_domain->data[STRESS_RAPL_DATA_STRESSOR].energy_uj = 0.0;

		if ((fp = fopen(path, "r")) != NULL) {
			if (fscanf(fp, "%lf\n", &rapl_domain->max_energy_uj) != 1)
				rapl_domain->max_energy_uj = 0.0;
			(void)fclose(fp);
		}
		(void)snprintf(path, sizeof(path),
			"/sys/class/powercap/%s/name",
			entry->d_name);

		rapl_domain->domain_name = NULL;
		if ((fp = fopen(path, "r")) != NULL) {
			char domain_name[128];

			if (fgets(domain_name, sizeof(domain_name), fp) != NULL) {
				domain_name[strcspn(domain_name, "\n")] = '\0';

				/* Truncate package name */
				if (!strncmp(domain_name, "package-", 8)) {
					char buf[sizeof(domain_name)];

					(void)snprintf(buf, sizeof(buf), "pkg-%s", domain_name + 8);
					(void)strncpy(domain_name, buf, sizeof(domain_name));
				}
				if (stress_rapl_domain_unique(*rapl_domains, domain_name))
					rapl_domain->domain_name = shim_strdup(domain_name);
			}
			(void)fclose(fp);
		}

		if (rapl_domain->domain_name == NULL) {
			free(rapl_domain->name);
			free(rapl_domain);
			continue;
		}

		rapl_domain->index = (size_t)n;
		stress_rapl_add_list(rapl_domains, rapl_domain);
		n++;
	}
	(void)closedir(dir);

	if (!n) {
		if (unreadable_energy_uj)
			pr_inf("device does not have any user readable RAPL domains, cannot measure power usage%s\n",
				stress_check_capability(SHIM_CAP_IS_ROOT) ? "" : "; perhaps run as root");
		else
			pr_inf("device does not have any RAPL domains, cannot measure power usage\n");
		return -1;
	}
	return n;
}

/*
 *  stress_rapl_get_power()
 *	get power discharge rate from system via the RAPL interface
 */
static int stress_rapl_get_power(stress_rapl_domain_t *rapl_domains, const int which)
{
	stress_rapl_domain_t *rapl_domain;
	int got_data = -1;

	if ((which < STRESS_RAPL_DATA_RAPLSTAT) || (which > STRESS_RAPL_DATA_STRESSOR))
		return -1;

	for (rapl_domain = rapl_domains; rapl_domain; rapl_domain = rapl_domain->next) {
		char path[PATH_MAX];
		FILE *fp;
		double ujoules;

		(void)snprintf(path, sizeof(path),
			"/sys/class/powercap/%s/energy_uj",
			rapl_domain->name);

		if ((fp = fopen(path, "r")) == NULL)
			continue;

		if (fscanf(fp, "%lf\n", &ujoules) == 1) {
			double t_now = stress_time_now();
			double t_delta = t_now - rapl_domain->data[which].time;
			double prev_energy_uj = rapl_domain->data[which].energy_uj;

			got_data = 0;

			/* Invalid, reuse prev value as a workaround */
			if (ujoules <= 0.0)
				ujoules = prev_energy_uj;

			/* ensure we have a valid value */
			if (ujoules > 0.0) {

				/* Wrapped around since previous time? */
				if (ujoules - rapl_domain->data[which].energy_uj < 0.0) {
				rapl_domain->data[which].energy_uj = ujoules;
					ujoules += rapl_domain->max_energy_uj;
				} else {
					rapl_domain->data[which].energy_uj = ujoules;
				}
				/* Time delta must be large enough to be reliable */
				if (t_delta >= 0.25) {
					const double power_watts = (ujoules - prev_energy_uj) / (t_delta * 1000000.0);

					/* Ignore updates for zero readings */
					if (power_watts > 0.0)  {
						rapl_domain->data[which].time = t_now;
						rapl_domain->data[which].power_watts = power_watts;
					}
				}
			}
		}
		(void)fclose(fp);
	}
	return got_data;
}

int stress_rapl_get_power_raplstat(stress_rapl_domain_t *rapl_domains)
{
	return stress_rapl_get_power(rapl_domains, STRESS_RAPL_DATA_RAPLSTAT);
}

/*
 *  stress_rapl_get_power_stressor()
 *	get per stressor power discharge rate from system via the RAPL interface
 */
int stress_rapl_get_power_stressor(stress_rapl_domain_t *rapl_domains, stress_rapl_t *rapl)
{
	int ret;
	size_t i;
	stress_rapl_domain_t *rapl_domain;

	ret = stress_rapl_get_power(rapl_domains, STRESS_RAPL_DATA_STRESSOR);
	if (!rapl)
		return ret;

	for (i = 0; i < STRESS_RAPL_DOMAINS_MAX; i++)
		rapl->power_watts[i] = 0.0;

	for (rapl_domain = rapl_domains; rapl_domain; rapl_domain = rapl_domain->next) {
		i = rapl_domain->index;
		if (i >= STRESS_RAPL_DOMAINS_MAX)
			continue;
		rapl->power_watts[i] = rapl_domain->data[STRESS_RAPL_DATA_STRESSOR].power_watts;
	}

	return 0;
}

/*
 *  stress_rapl_dump()
 *	dump rapl power measurements
 */
void stress_rapl_dump(FILE *yaml, stress_stressor_t *stressors_list, stress_rapl_domain_t *rapl_domains)
{
	bool no_rapl_stats = true;
	stress_stressor_t *ss;

	pr_yaml(yaml, "rapl-power-domains:\n");

	for (ss = stressors_list; ss; ss = ss->next) {
		bool dumped_heading = false;
		bool print_nl = false;
		stress_rapl_domain_t *rapl_domain;

		if (ss->ignore.run)
			continue;

		for (rapl_domain = rapl_domains; rapl_domain; rapl_domain = rapl_domain->next) {
			double harmonic_total = 0.0;
			int count = 0;
			int32_t j;
			size_t i;

			i = rapl_domain->index;
			if (i >= STRESS_RAPL_DOMAINS_MAX)
				continue;

			for (j = 0; j < ss->instances; j++) {
				double power = ss->stats[j]->rapl.power_watts[i];

				if (power > 0.0) {
					harmonic_total += 1.0 / power;
					count++;
				}
			}

			if (harmonic_total > 0.0) {
				const double harmonic_mean = (double)count / harmonic_total;

                                if (!dumped_heading) {
					dumped_heading = true;

					pr_inf("%s:\n", ss->stressor->name);
					pr_yaml(yaml, "    - stressor: %s\n", ss->stressor->name);
                                }

				pr_inf(" %-19s %8.2f W\n", rapl_domain->domain_name, harmonic_mean);
				pr_yaml(yaml, "      %s: %.2f\n", rapl_domain->domain_name, harmonic_mean);
				no_rapl_stats = false;
				print_nl = true;
			}
		}
		if (print_nl)
			pr_yaml(yaml, "\n");
	}

	if (no_rapl_stats)
		pr_inf("RAPL power measurements not available\n");
}
#endif
