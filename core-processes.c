// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"

#if defined(__linux__)
static int stress_dump_processes_filter(const struct dirent *d)
{
	if (!d)
		return 0;

	return isdigit((int)d->d_name[0]);
}

void stress_dump_processes(void)
{
	int i, n, pid_width = 5;

	struct dirent **namelist;

	n = scandir("/proc", &namelist, stress_dump_processes_filter, alphasort);
	if (n <= 0)
		return;

	for (i = 0; i < n; i++) {
		const int len = strlen(namelist[i]->d_name);

		pid_width = STRESS_MAXIMUM(pid_width, len);
	}

	for (i = 0; i < n; i++) {
		char path[PATH_MAX];
		struct stat statbuf;
		char buf[8192];
		char cmd[4096];
		char state[16];
		char name[32];
		char *p_name;
		pid_t pid, ppid;
		ssize_t ret;

		(void)shim_memset(cmd, 0, sizeof(cmd));
		(void)snprintf(path, sizeof(path), "/proc/%s/cmdline", namelist[i]->d_name);
		ret = stress_system_read(path, cmd, sizeof(cmd));
		if (ret > 0) {
			int j;

			for (j = 0; j < (int)ret; j++) {
				if (cmd[j] == '\0')
					cmd[j] = ' ';
			}
			for (j--; j > 0; j--) {
				if (cmd[j] == ' ')
					cmd[j] = '\0';
				else
					break;
			}
		}
		if (strstr(cmd, "stress-ng") == NULL)
			continue;

		pid = (pid_t)atoi(namelist[i]->d_name);
		ppid = 0;
		p_name = "?";
		(void)shim_strlcpy(state, "?", sizeof(state));
		(void)snprintf(path, sizeof(path), "/proc/%s", namelist[i]->d_name);
		if (stat(path, &statbuf) == 0) {
#if defined(BUILD_STATIC)
			(void)snprintf(name, sizeof(name), "%u", (unsigned int)statbuf.st_uid);
			p_name = name;
#else
			struct passwd *pwd;

			pwd = getpwuid(statbuf.st_uid);
			if (pwd && pwd->pw_name) {
				p_name = pwd->pw_name;
			} else {
				(void)snprintf(name, sizeof(name), "%u", (unsigned int)statbuf.st_uid);
				p_name = name;
			}
#endif
		}

		(void)snprintf(path, sizeof(path), "/proc/%s/status", namelist[i]->d_name);
		ret = stress_system_read(path, buf, sizeof(buf));
		if (ret > 0) {
			char *ptr;

			ptr = strstr(buf, "\nPPid:");
			if (ptr) {
				if (sscanf(ptr, "\nPPid:%d", &ppid) != 1)
					ppid = 0;
			}
			(void)shim_strlcpy(state, "?", sizeof(state));
			ptr = strstr(buf, "\nState:");
			if (ptr) {
				if (sscanf(ptr, "\nState:%1s", state) != 1)
					(void)shim_strlcpy(state, "?", sizeof(state));
			}
		}
		pr_inf("proc: %-8.8s %*d %*d %c %s\n", p_name, pid_width, pid, pid_width, ppid, state[0], cmd);
	}
	stress_dirent_list_free(namelist, n);
}
#else
void stress_dump_processes(void)
{
}
#endif
