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
#include "core-config-check.h"

#include <ctype.h>

#if defined(HAVE_TERMIO_H)
#include <termio.h>
#endif

#if defined(__linux__)
static int stress_config_check_cpu_filter(const struct dirent *d)
{
        if (UNLIKELY(!d))
                return 0;
	if (UNLIKELY(strlen(d->d_name) < 4))
		return 0;
        if (strncmp(d->d_name, "cpu", 3))
		return 0;
	if (isdigit((unsigned char)d->d_name[3]))
		return 1;
	return 0;
}

/*
 *  stress_config_read()
 *	read a uint64_t value from a file
 */
static int stress_config_read(const char *path, uint64_t *value)
{
	char buffer[256];

	if (UNLIKELY(stress_system_read(path, buffer, sizeof(buffer)) < 0))
		return -1;
	if (UNLIKELY(!*buffer))
		return -1;
	if (UNLIKELY(sscanf(buffer, "%" SCNu64, value) < 0))
		return -1;
	return 0;
}
#endif

/*
 *  stress_config_check()
 *	sanity check system configuration and inform user if
 *	any sub-optimal performance configurations are being used
 */
void stress_config_check(void)
{
	size_t shmall = 0, freemem = 0, totalmem = 0, freeswap = 0, totalswap = 0, freetotal;

#if defined(__linux__)
	{
		char path[PATH_MAX];
		const char *str;
		bool is_snap = false;

		str = getenv("SNAP_NAME");
		if (str) {
			char *lowstr = strdup(str);

			if (lowstr) {
				char *ptr;

				for (ptr = lowstr; *ptr; ptr++)
					*ptr = (char)tolower((int)*ptr);

				if (strstr(lowstr, "stress"))
					is_snap = true;
				free(lowstr);
			}
		}
		str = stress_get_proc_self_exe(path, sizeof(path));
		if (str) {
			if (strncmp("/snap/", path, 6) == 0)
				is_snap = true;
		}
		if (is_snap) {
			bool oom_avoid = true;

			pr_warn("note: stress-ng appears to be a snap and may not "
				"run correctly inside this unsupported environment\n");
			pr_inf("note: forcibly enabling --oom-avoid for this environment\n");
			stress_set_setting_global("oom-avoid", TYPE_ID_BOOL, &oom_avoid);
		}
	}

	if (g_opt_flags & OPT_FLAGS_METRICS) {
		static const char autogroup_path[] = "/proc/sys/kernel/sched_autogroup_enabled";
		static const char cpu_path[] = "/sys/devices/system/cpu";
		static const char boost_path[] = "/sys/devices/system/cpu/cpufreq/boost";
		uint64_t value;
		struct dirent **namelist;
		int n, i;
		int powersave = 0;

		if ((stress_config_read(autogroup_path, &value) != -1) && (value > 0)) {
#if defined(HAVE_TERMIO_H) &&	\
    defined(TCGETS)
			struct termios t;
			int ret;

			ret = ioctl(fileno(stdout), TCGETS, &t);
			if ((ret < 0) && (errno == ENOTTY)) {
				pr_inf("note: %s is %" PRIu64 " and this can impact "
					"scheduling throughput for processes not "
					"attached to a tty. Setting this to 0 may "
					"improve performance metrics\n", autogroup_path, value);
			}
#endif
		}

		if ((stress_config_read(boost_path, &value) != -1) && (value == 0)) {
			pr_inf("note: boost is disabled and this may impact "
				"top performance; setting %s to 1 may improve "
				"performance.", boost_path);
		}

		n = scandir(cpu_path, &namelist, stress_config_check_cpu_filter, alphasort);
		for (i = 0; i < n; i++) {
			char filename[PATH_MAX];
			char buffer[64];

			(void)snprintf(filename, sizeof(filename), "%s/%s/cpufreq/scaling_governor", cpu_path, namelist[i]->d_name);
			if (UNLIKELY(stress_system_read(filename, buffer, sizeof(buffer)) < 0))
				continue;
			if (strncmp(buffer, "powersave", 9) == 0)
				powersave++;
		}
		stress_dirent_list_free(namelist, n);
		if (powersave > 0) {
			pr_inf("note: %d cpus have scaling governors set to "
				"powersave and this may impact performance; "
				"setting %s/cpu*/cpufreq/scaling_governor to "
				"'performance' may improve performance\n",
				powersave, cpu_path);
		}
	}
#endif
	stress_get_memlimits(&shmall, &freemem, &totalmem, &freeswap, &totalswap);
	freetotal = freemem + freeswap;
	if (((freemem > 0) && (freemem < (size_t)(256 * MB))) ||
	    ((freetotal > 0) && (freetotal < (size_t)(512 * MB)))) {
		pr_inf("note: system has only %zu MB of free memory and swap, "
			"recommend using --oom-avoid\n", freetotal / (size_t)MB);
	}
}
