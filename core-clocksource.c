// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-clocksource.h"

#if defined(__linux__)
static inline void stress_clocksource_tolower(char *str)
{
	while (*str) {
		if (isupper((int)*str))
			*str = (char)tolower((int)*str);
		str++;
	}
}
#endif

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
		ret = stress_system_read(path, buf, sizeof(buf));
		if (ret > 0) {
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
