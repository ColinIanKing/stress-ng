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
#include "core-clocksource.h"

#include <ctype.h>

#if defined(__linux__)
static inline void stress_clocksource_tolower(char *str)
{
	unsigned char ch;

	while ((ch = (unsigned char)*str)) {
		if (isupper(ch))
			*str = (char)tolower(ch);
		str++;
	}
}
#endif

/*
 *  stress_clocksource_check()
 *	check the clocksource being used, warn if the less accurate
 *	HPET is being used
 */
void stress_clocksource_check(void)
{
#if defined(__linux__)
	DIR *dir;
	struct dirent *entry;
	static bool warned = false;
	static const char dirname[] = "/sys/devices/system/clocksource";

	if (warned)
		return;

	dir = opendir(dirname);
	if (!dir)
		return;

	while ((entry = readdir(dir))) {
		char path[PATH_MAX];
		char buf[256];
		ssize_t ret;

		if (strncmp(entry->d_name, "clocksource", 11))
			continue;

		(void)snprintf(path, sizeof(path), "%s/%s/current_clocksource", dirname, entry->d_name);
		ret = stress_system_read(path, buf, sizeof(buf) - 1);
		if (ret > 0) {
			buf[ret] = '\0';
			stress_clocksource_tolower(buf);
			if (strncmp(buf, "hpet", 4) == 0) {
				pr_warn("WARNING! using HPET clocksource (refer to %s/%s), this may impact "
					"benchmarking performance\n", dirname, entry->d_name);
				warned = true;
			}
		}
	}
	(void)closedir(dir);
#endif
}
