/*
 * Copyright (C) 2023      Colin Ian King.
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
#include "core-config-check.h"

#if defined(__linux__)
static int stress_config_check_cpu_filter(const struct dirent *d)
{
        if (!d)
                return 0;
	if (strlen(d->d_name) < 4)
		return 0;
        if (strncmp(d->d_name, "cpu", 3))
		return 0;
	if (isdigit((int)d->d_name[3]))
		return 1;
	return 0;
}

static int stress_config_read(const char *path, uint64_t *value)
{
	char buffer[256];

	if (stress_system_read(path, buffer, sizeof(buffer)) < 0)
		return -1;
	if (!*buffer)
		return -1;
	if (sscanf(buffer, "%" SCNu64, value) < 0)
		return -1;
	return 0;
}
#endif

void stress_config_check(void)
{
#if defined(__linux__)
	{
		static char path[] = "/proc/sys/kernel/sched_autogroup_enabled";
		uint64_t value;

		if ((stress_config_read(path, &value) != -1) && (value > 0)) {
			pr_inf("note: %s is %" PRIu64 " and this can impact "
				"scheduling throughput for processes not "
				"attached to a tty. Setting this to 0 may "
				"improve performance metrics\n", path, value);
		}
	}

	{
        	struct dirent **namelist;
		static char path[] = "/sys/devices/system/cpu";
		int n, i;
		int powersave = 0;

        	n = scandir(path, &namelist, stress_config_check_cpu_filter, alphasort);
		for (i = 0; i < n; i++) {
			char filename[PATH_MAX];
			char buffer[64];
	
			(void)snprintf(filename, sizeof(filename), "%s/%s/cpufreq/scaling_governor", path, namelist[i]->d_name);
			if (stress_system_read(filename, buffer, sizeof(buffer)) < 0)
				continue;
			if (strncmp(buffer, "powersave", 9) == 0)
				powersave++;
			
		}
		stress_dirent_list_free(namelist, n);
		if (powersave > 0) {
			pr_inf("note: %d cpus have scaling governors set to "
				"powersave and this can impact on performance; "
				"setting %s/cpu*/cpufreq/scaling_governor to "
				"'performance' will improve performance\n",
				powersave, path);
		}
	}
#endif
}
