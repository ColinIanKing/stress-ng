/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-attribute.h"
#include "core-affinity.h"
#include "core-builtin.h"

#include <ctype.h>
#include <sched.h>
#include <sys/types.h>
#include <dirent.h>

static const char option[] = "taskset";

#if defined(HAVE_SCHED_GETAFFINITY) && \
    defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)

static cpu_set_t stress_affinity_cpu_set;

/*
 * stress_check_cpu_affinity_range()
 * @max_cpus: maximum cpus allowed, 0..N-1
 * @cpu: cpu number to check
 */
static void stress_check_cpu_affinity_range(
	const int32_t max_cpus,
	const int32_t cpu)
{
	if ((cpu < 0) || ((max_cpus != -1) && (cpu >= max_cpus))) {
		(void)fprintf(stderr, "%s: invalid range, %" PRId32 " is not allowed, "
			"allowed range: 0 to %" PRId32 "\n", option,
			cpu, max_cpus - 1);
		_exit(EXIT_FAILURE);
	}
}

/*
 * stress_parse_cpu()
 * @str: parse string containing decimal CPU number
 *
 * Returns: cpu number, or exits the program on invalid number in str
 */
static int stress_parse_cpu(const char *const str)
{
	int val;

	if (sscanf(str, "%d", &val) != 1) {
		(void)fprintf(stderr, "%s: invalid number '%s'\n", option, str);
		_exit(EXIT_FAILURE);
	}
	return val;
}

/*
 * stress_set_cpu_affinity_current()
 * @set: cpu_set to set the cpu affinity to current process
 */
static void stress_set_cpu_affinity_current(cpu_set_t *set)
{
	if (sched_setaffinity(0, sizeof(cpu_set_t), set) < 0) {
		pr_err("%s: cannot set CPU affinity, errno=%d (%s)\n",
			option, errno, strerror(errno));
		_exit(EXIT_FAILURE);
	}
	(void)shim_memcpy(&stress_affinity_cpu_set, set, sizeof(stress_affinity_cpu_set));
}

/*
 *  stress_get_topology_set()
 *	find cpus in a specific topology
 */
static void stress_get_topology_set(
	const char *topology_list,
	const char *topology,
	const char *arg,
	cpu_set_t *set,
	int *setbits)
{
	DIR *dir;
	static const char path[] = "/sys/devices/system/cpu";
	const struct dirent *d;
	int max_cpus = (int)stress_get_processors_configured();
	cpu_set_t *sets;
	int i, n_sets = 0, which;

	if (sscanf(arg + strlen(topology) , "%d", &which) != 1) {
		(void)fprintf(stderr, "%s: invalid argument '%s' missing integer\n", topology, arg);
		_exit(EXIT_FAILURE);
	}

	/* Must be at most max_cpus worth of packages (over estimated) */
	sets = calloc(max_cpus, sizeof(*sets));
	if (!sets) {
		(void)fprintf(stderr, "%s: cannot allocate %d cpusets, aborting\n", option, max_cpus);
		_exit(EXIT_FAILURE);
	}

	dir = opendir(path);
	if (!dir) {
		(void)fprintf(stderr, "%s: cannot scan '%s', %s option not available\n",
			option, topology, path);
		free(sets);
		_exit(EXIT_FAILURE);
	}

	while ((d = readdir(dir)) != NULL) {
		char filename[PATH_MAX];
		char str[1024], *ptr, *token;
		cpu_set_t newset;
		int lo, hi;

		if (strncmp(d->d_name, "cpu", 3))
			continue;
		if (!isdigit((unsigned char)d->d_name[3]))
			continue;

		(void)snprintf(filename, sizeof(filename), "%s/%s/topology/%s", path, d->d_name, topology_list);

		if (stress_system_read(filename, str, sizeof(str)) < 1)
			continue;

		CPU_ZERO(&newset);
		for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
			const char *tmpptr = strstr(token, "-");

			if (sscanf(token, "%d", &i) != 1)
				continue;
			if (i >= CPU_SETSIZE)
				i = CPU_SETSIZE - 1;

			lo = hi = i;
			if (tmpptr) {
				tmpptr++;
				if (sscanf(tmpptr, "%d", &i) != 1)
					continue;
				hi = i;
			}

			for (i = lo; i <= hi; i++)
				CPU_SET(i, &newset);
		}

		for (i = 0; i < n_sets; i++) {
			if (CPU_EQUAL(&sets[i], &newset))
				break;
		}
		/* not found, it's a new package cpu list */
		if (i == n_sets) {
			(void)shim_memcpy(&sets[i], &newset, sizeof(newset));
			n_sets++;
		}
	}
	(void)closedir(dir);

	if (which >= n_sets) {
		if (n_sets > 1)
			(void)fprintf(stderr, "%s: %s %d not found, only %ss 0-%d available\n",
				option, topology, which, topology, n_sets - 1);
		else
			(void)fprintf(stderr, "%s: %s %d not found, only %s 0 available\n",
				option, topology, which, topology);
		free(sets);
		_exit(EXIT_FAILURE);
	}
	CPU_OR(set, set, &sets[which]);
	*setbits = CPU_COUNT(set);

	free(sets);
}

/*
 * stress_set_cpu_affinity()
 * @arg: list of CPUs to set affinity to, comma separated
 * @set: cpuset to set
 * @setbits: number of bits set
 *
 * Returns: 0 - OK
 */
int stress_parse_cpu_affinity(const char *arg, cpu_set_t *set, int *setbits)
{
	char *str, *ptr, *token;
	const int32_t max_cpus = stress_get_processors_configured();
	int i;

	*setbits = 0;
	CPU_ZERO(set);

	str = stress_const_optdup(arg);
	if (!str) {
		(void)fprintf(stderr, "out of memory duplicating argument '%s'\n", arg);
		_exit(EXIT_FAILURE);
	}

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		int lo, hi;
		const char *tmpptr = strstr(token, "-");

		if (!strcmp(token, "odd")) {
			for (i = 1; i < max_cpus; i += 2) {
				if (!CPU_ISSET(i, set)) {
					CPU_SET(i, set);
					(*setbits)++;
				}
			}
			continue;
		} else if (!strcmp(token, "even")) {
			for (i = 0; i < max_cpus; i += 2) {
				if (!CPU_ISSET(i, set)) {
					CPU_SET(i, set);
					(*setbits)++;
				}
			}
			continue;
		} else if (!strcmp(token, "all")) {
			for (i = 0; i < max_cpus; i++) {
				if (!CPU_ISSET(i, set)) {
					CPU_SET(i, set);
					(*setbits)++;
				}
			}
			continue;
		} else if (!strcmp(token, "random")) {
			for (i = 0; i < max_cpus; i++) {
				if (stress_mwc1()) {
					if (!CPU_ISSET(i, set)) {
						CPU_SET(i, set);
						(*setbits)++;
					}
				}
			}
			if (*setbits == 0) {
				i = stress_mwc32modn((uint32_t)max_cpus);

				if (!CPU_ISSET(i, set)) {
					CPU_SET(i, set);
					(*setbits)++;
				}
			}
			continue;
		} else if (!strncmp(token, "package", 7)) {
			stress_get_topology_set("package_cpus_list", "package", token, set, setbits);
			continue;
		} else if (!strncmp(token, "cluster", 7)) {
			stress_get_topology_set("cluster_cpus_list", "cluster", token, set, setbits);
			continue;
		} else if (!strncmp(token, "die", 3)) {
			stress_get_topology_set("die_cpus_list", "die", token, set, setbits);
			continue;
		} else if (!strncmp(token, "core", 4)) {
			stress_get_topology_set("core_cpus_list", "core", token, set, setbits);
			continue;
		}

		hi = lo = stress_parse_cpu(token);
		if (tmpptr) {
			tmpptr++;
			if (*tmpptr)
				hi = stress_parse_cpu(tmpptr);
			else {
				(void)fprintf(stderr, "%s: expecting number following "
					"'-' in '%s'\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
			if (hi < lo) {
				(void)fprintf(stderr, "%s: invalid range in '%s' "
					"(end value must be larger than "
					"start value)\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
		}
		stress_check_cpu_affinity_range(max_cpus, lo);
		stress_check_cpu_affinity_range(max_cpus, hi);

		for (i = lo; i <= hi; i++) {
			if (!CPU_ISSET(i, set)) {
				CPU_SET(i, set);
				(*setbits)++;
			}
		}
	}
	free(str);

	if (*setbits)
		stress_set_cpu_affinity_current(set);

	return 0;
}

/*
 * stress_set_cpu_affinity()
 * @arg: list of CPUs to set affinity to, comma separated
 *
 * Returns: 0 - OK
 */
int stress_set_cpu_affinity(const char *arg)
{
	cpu_set_t set;
	int setbits, ret;

	ret = stress_parse_cpu_affinity(arg, &set, &setbits);
	if ((ret == 0) && (setbits))
		stress_set_cpu_affinity_current(&set);
	return ret;
}

/*
 *  stress_change_cpu()
 *	try and change process to a different CPU.
 *	old_cpu: the cpu to change from, -ve = current cpu (that we don't want to use)
 *					 +ve = cpu don't want to ever use
 */
int stress_change_cpu(stress_args_t *args, const int old_cpu)
{
	int from_cpu;

	cpu_set_t mask;
	(void)args;

	/* only change cpu when --change-cpu is enabled */
	if ((g_opt_flags & OPT_FLAGS_CHANGE_CPU) == 0)
		return old_cpu;

	if (CPU_COUNT(&stress_affinity_cpu_set) == 0) {
		if (sched_getaffinity(0, sizeof(mask), &mask) < 0)
			return old_cpu;		/* no dice */
	} else {
		shim_memcpy(&mask, &stress_affinity_cpu_set, sizeof(mask));
	}

	if (old_cpu < 0) {
		from_cpu = (int)stress_get_cpu();
	} else {
		from_cpu = old_cpu;

		/* Try hard not to use the CPU we came from */
		if (CPU_COUNT(&mask) > 1)
			CPU_CLR((int)from_cpu, &mask);
	}

	if (sched_setaffinity(0, sizeof(mask), &mask) >= 0) {
		const int moved_cpu = (int)stress_get_cpu();
		/*
		pr_dbg("%s: process [%" PRIdMAX "] (child of instance %d on CPU %u moved to CPU %u)\n",
			args->name, (intmax_t)getpid(), args->instance, from_cpu, moved_cpu);
		*/
		return moved_cpu;
	}
	return (int)from_cpu;
}

#else
int PURE stress_change_cpu(stress_args_t *args, const int old_cpu)
{
	(void)args;

	/* no change */
	return old_cpu;
}

int stress_set_cpu_affinity(const char *arg)
{
	(void)arg;

	(void)fprintf(stderr, "%s: setting CPU affinity not supported\n", option);
	_exit(EXIT_FAILURE);
}
#endif

/*
 *  stress_get_usable_cpus()
 *	get an uint32_t array of cpu numbers of usable cpus.
 */
uint32_t stress_get_usable_cpus(uint32_t **cpus, const bool use_affinity)
{
	uint32_t i, n_cpus = stress_get_processors_configured();

#if defined(HAVE_SCHED_GETAFFINITY) && \
    defined(HAVE_SCHED_SETAFFINITY) && \
    defined(HAVE_CPU_SET_T)
	if (use_affinity) {
		/* if affinity has been set.. */
		if (CPU_COUNT(&stress_affinity_cpu_set) > 0) {
			uint32_t n;

			/* don't want to overrun the cpu set */
			n_cpus = STRESS_MINIMUM(n_cpus, CPU_SETSIZE);

			for (n = 0, i = 0; i < n_cpus; i++) {
				if (CPU_ISSET((int)i, &stress_affinity_cpu_set))
					n++;
			}
			if (n == 0) {
				/* Should not happen */
				*cpus = NULL;
				return 0;
			}
			*cpus = (uint32_t *)calloc(n, sizeof(**cpus));
			if (*cpus == NULL)
				return 0;

			for (n = 0, i = 0; i < n_cpus; i++) {
				if (CPU_ISSET((int)i, &stress_affinity_cpu_set)) {
					(*cpus)[n] = i;
					n++;
				}
			}
			return n;
		}
	}
#else
	(void)use_affinity;
#endif
	if (n_cpus == 0) {
		/* Should not happen */
		*cpus = NULL;
		return 0;
	}

	*cpus = (uint32_t *)malloc(sizeof(**cpus) * n_cpus);
	if (*cpus == NULL)
		return 0;

	for (i = 0; i < n_cpus; i++) {
		(*cpus)[i] = i;
	}
	return n_cpus;
}

/*
 *  stress_free_usable_cpus()
 *	free *cpus
 */
void stress_free_usable_cpus(uint32_t **cpus)
{
	if (!cpus)
		return;
	if (*cpus)
		free(*cpus);
	*cpus = NULL;
}
