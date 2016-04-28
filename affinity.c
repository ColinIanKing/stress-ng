/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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

static const char *option = "taskset";

#if defined(__linux__)

#include <sched.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>


static void check_cpu_affinity_range(
	const int32_t max_cpus,
	const int32_t cpu)
{
	if ((cpu < 0) || ((max_cpus != -1) && (cpu >= max_cpus))) {
		fprintf(stderr, "%s: invalid range, %" PRId32 " is not allowed, "
			"allowed range: 0 to %" PRId32 "\n", option, cpu, max_cpus - 1);
		exit(EXIT_FAILURE);
	}
}

static int get_cpu(char *const str)
{
	int val;

	if (sscanf(str, "%d", &val) != 1) {
		fprintf(stderr, "%s: invalid number '%s'\n", option, str);
		exit(EXIT_FAILURE);
	}
	return val;
}

int set_cpu_affinity(char *const arg)
{
	cpu_set_t set;
	char *str, *token;
	int32_t max_cpus = stress_get_processors_configured();

	CPU_ZERO(&set);

	for (str = arg; (token = strtok(str, ",")) != NULL; str = NULL) {
		int i, lo, hi;
		char *ptr = strstr(token, "-");

		hi = lo = get_cpu(token);
		if (ptr) {
			ptr++;
			if (*ptr)
				hi = get_cpu(ptr);
			else {
				fprintf(stderr, "%s: expecting number following "
					"'-' in '%s'\n", option, token);
				exit(EXIT_FAILURE);
			}
			if (hi <= lo) {
				fprintf(stderr, "%s: invalid range in '%s' "
					"(end value must be larger than "
					"start value\n", option, token);
				exit(EXIT_FAILURE);
			}
		}
		check_cpu_affinity_range(max_cpus, lo);
		check_cpu_affinity_range(max_cpus, hi);

		for (i = lo; i <= hi; i++)
			CPU_SET(i, &set);
	}
	if (sched_setaffinity(getpid(), sizeof(set), &set) < 0) {
		pr_err(stderr, "%s: cannot set CPU affinity, errno=%d (%s)\n",
			option, errno, strerror(errno));
		exit(EXIT_FAILURE);
	}

	return 0;
}

#else
int set_cpu_affinity(char *const arg)
{
	(void)arg;

	fprintf(stderr, "%s: setting CPU affinity not supported\n", option);
	exit(EXIT_FAILURE);
}
#endif
