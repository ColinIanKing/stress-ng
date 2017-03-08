/*
 * Copyright (C) 2016-2017 Canonical, Ltd.
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

#if defined(__linux__)

/*
 *  stress_cpu_online_set()
 *	set a specified CPUs online or offline
 */
static int stress_cpu_online_set(
	const args_t *args,
	const int32_t cpu,
	const int setting)
{
	char filename[PATH_MAX];
	char data[3];
	ssize_t ret;
	int fd, rc = EXIT_SUCCESS;

	(void)snprintf(filename, sizeof(filename),
		"/sys/devices/system/cpu/cpu%" PRId32 "/online", cpu);
	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		pr_fail_err("open");
		return EXIT_FAILURE;
	}

	data[0] = '0' + setting;
	data[1] = '\n';
	data[2] = '\0';

	ret = write(fd, data, 3);

	if (ret != 3) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
			pr_fail_err("write");
			rc = EXIT_FAILURE;
		}
	}
	(void)close(fd);
	return rc;
}

/*
 *  stress_cpu_online
 *	stress twiddling CPUs online/offline
 */
int stress_cpu_online(const args_t *args)
{
	const int32_t cpus = stress_get_processors_configured();
	int32_t i, cpu_online_count = 0;
	bool *cpu_online;
	int rc = EXIT_SUCCESS;

	if (geteuid() != 0) {
		if (args->instance == 0)
			pr_inf("%s: need root privilege to run "
				"this stressor\n", args->name);
		/* Not strictly a test failure */
		return EXIT_SUCCESS;
	}

	if ((cpus < 1) || (cpus > 65536)) {
		pr_inf("%s: too few or too many CPUs (found %" PRId32 ")\n", args->name, cpus);
		return EXIT_FAILURE;
	}

	cpu_online = calloc(cpus, sizeof(bool));
	if (!cpu_online) {
		pr_err("%s: out of memory\n", args->name);
		return EXIT_FAILURE;
	}

	/*
	 *  Determine how many CPUs we can online/offline via
	 *  the online sys interface
	 */
	for (i = 0; i < cpus; i++) {
		char filename[PATH_MAX];
		int ret;

		(void)snprintf(filename, sizeof(filename),
			"/sys/devices/system/cpu/cpu%" PRId32 "/online", i);
		ret = access(filename, O_RDWR);
		if (ret == 0) {
			cpu_online[i] = true;
			cpu_online_count++;
		}
	}
	if (cpu_online_count == 0) {
		pr_inf("%s: no CPUs can be set online/offline\n", args->name);
		free(cpu_online);
		return EXIT_FAILURE;
	}

	/*
	 *  Now randomly offline/online them all
	 */
	do {
		unsigned long cpu = mwc32() % cpus;
		if (cpu_online[cpu]) {
			rc = stress_cpu_online_set(args, cpu, 0);
			if (rc != EXIT_SUCCESS)
				break;
			rc = stress_cpu_online_set(args, cpu, 1);
			if (rc != EXIT_SUCCESS)
				break;
			inc_counter(args);
		}
	} while (keep_stressing());

	/*
	 *  Force CPUs all back online
	 */
	for (i = 0; i < cpus; i++) {
		if (cpu_online[i])
			(void)stress_cpu_online_set(args, i, 1);
	}
	free(cpu_online);

	return EXIT_SUCCESS;
}
#else
int stress_cpu_online(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
