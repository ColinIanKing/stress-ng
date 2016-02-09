/*
 * Copyright (C) 2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_CPU_ONLINE)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

/*
 *  stress_cpu_online_set()
 *	set a specified CPUs online or offline
 */
int stress_cpu_online_set(
	const char *name,
	const int32_t cpu,
	const int setting)
{
	char filename[PATH_MAX];
	char data[3];
	ssize_t ret;
	int fd, rc = EXIT_SUCCESS;

	snprintf(filename, sizeof(filename),
		"/sys/devices/system/cpu/cpu%" PRId32 "/online", cpu);
	fd = open(filename, O_WRONLY);
	if (fd < 0) {
		pr_fail_err(name, "open");
		return EXIT_FAILURE;
	}

	data[0] = '0' + setting;
	data[1] = '\n';
	data[2] = '\0';

	ret = write(fd, data, 3);

	if (ret != 3) {
		if ((errno != EAGAIN) && (errno != EINTR)) {
			pr_fail_err(name, "write");
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
int stress_cpu_online(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	const int32_t cpus = stress_get_processors_configured();
	int32_t i, cpu_online_count = 0;
	bool *cpu_online;
	int rc = EXIT_SUCCESS;

	(void)instance;

	if (geteuid() != 0) {
		if (instance == 0)
			pr_inf(stderr, "%s: need root privilege to run "
				"this stressor\n", name);
		/* Not strictly a test failure */
		return EXIT_SUCCESS;
	}

	if ((cpus < 1) || (cpus > 65536)) {
		pr_inf(stderr, "%s: too few or too many CPUs (found %" PRId32 ")\n", name, cpus);
		return EXIT_FAILURE;
	}

	cpu_online = calloc(cpus, sizeof(bool));
	if (!cpu_online) {
		pr_err(stderr, "%s: out of memory\n", name);
		return EXIT_FAILURE;
	}

	/*
	 *  Determine how many CPUs we can online/offline via
	 *  the online sys interface
	 */
	for (i = 0; i < cpus; i++) {
		char filename[PATH_MAX];
		int ret;

		snprintf(filename, sizeof(filename),
			"/sys/devices/system/cpu/cpu%" PRId32 "/online", i);
		ret = access(filename, O_RDWR);
		if (ret == 0) {
			cpu_online[i] = true;
			cpu_online_count++;
		}
	}
	if (cpu_online_count == 0) {
		pr_inf(stderr, "%s: no CPUs can be set online/offline\n", name);
		free(cpu_online);
		return EXIT_FAILURE;
	}

	/*
	 *  Now randomly offline/online them all
	 */
	do {
		unsigned long cpu = mwc32() % cpus;
		if (cpu_online[cpu]) {
			rc = stress_cpu_online_set(name, cpu, 0);
			if (rc != EXIT_SUCCESS)
				break;
			rc = stress_cpu_online_set(name, cpu, 1);
			if (rc != EXIT_SUCCESS)
				break;
			(*counter)++;
		}
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/*
	 *  Force CPUs all back online
	 */
	for (i = 0; i < cpus; i++) {
		if (cpu_online[i])
			(void)stress_cpu_online_set(name, i, 1);
	}
	free(cpu_online);

	return EXIT_SUCCESS;
}

#endif
