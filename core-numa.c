/*
 * Copyright (C) 2022-2024 Colin Ian King.
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
#include "core-numa.h"

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

static const char option[] = "option --mbind";

#if defined(__NR_get_mempolicy) &&      \
    defined(__NR_mbind) &&              \
    defined(__NR_migrate_pages) &&      \
    defined(__NR_move_pages) &&         \
    defined(__NR_set_mempolicy) &&	\
    defined(HAVE_LINUX_MEMPOLICY_H)
/*
 * stress_check_numa_range()
 * @max_node: maximum NUMA node allowed, 0..N
 * @node: node number to check
 */
static void stress_check_numa_range(
	const unsigned long max_node,
	const unsigned long node)
{
	if (node >= max_node) {
		if (max_node > 1) {
			(void)fprintf(stderr, "%s: invalid range, %lu is not allowed, "
				"allowed range: 0 to %lu\n", option,
				node, max_node - 1);
		} else {
			(void)fprintf(stderr, "%s: invalid range, %lu is not allowed, "
				"allowed range: 0\n", option, node);
		}
		_exit(EXIT_FAILURE);
	}
}

/*
 *  stress_numa_count_mem_nodes()
 *	determine the number of NUMA memory nodes
 */
int stress_numa_count_mem_nodes(unsigned long *max_node)
{
	FILE *fp;
	unsigned long node_id = 0;
	char buffer[8192], *ptr;
	const char *str;
	long n = 0;

	*max_node = 0;

	fp = fopen("/proc/self/status", "r");
	if (!fp)
		return -1;

	while (fgets(buffer, sizeof(buffer), fp)) {
		if (!strncmp(buffer, "Mems_allowed:", 13)) {
			str = buffer + 13;
			break;
		}
	}
	(void)fclose(fp);

	if (!str)
		return -1;

	ptr = buffer + strlen(buffer) - 2;

	/*
	 *  Parse hex digits into NUMA node ids, these
	 *  are listed with least significant node last
	 *  so we need to scan backwards from the end of
	 *  the string back to the start.
	 */
	while ((*ptr != ' ') && (ptr > str)) {
		int i;
		unsigned int val;

		/* Skip commas */
		if (*ptr == ',') {
			ptr--;
			continue;
		}

		if (sscanf(ptr, "%1x", &val) != 1)
			return -1;

		/* Each hex digit represent 4 memory nodes */
		for (i = 0; i < 4; i++) {
			if (val & (1 << i))
				n++;
			node_id++;
			if (*max_node < node_id)
				*max_node = node_id;
		}
		ptr--;
	}

	return n;
}

/*
 *  stress_numa_nodes()
 *	determine the number of NUMA memory nodes,
 *	always returns at least 1 if no nodes found,
 *	useful for cache size scaling by node count
 */
int stress_numa_nodes(void)
{
	unsigned long max_node = 0;
	static int nodes = -1;	/* used as a cached copy */

	if (nodes == -1) {
		nodes = stress_numa_count_mem_nodes(&max_node);
		if (nodes < 1)
			nodes = 1;
	}
	return nodes;
}

/*
 * stress_parse_node()
 * @str: parse string containing decimal NUMA node number
 *
 * Returns: NUMA node number, or exits the program on invalid number in str
 */
static unsigned long stress_parse_node(const char *const str)
{
	unsigned long val;

	if (sscanf(str, "%lu", &val) != 1) {
		(void)fprintf(stderr, "%s: invalid number '%s'\n", option, str);
		_exit(EXIT_FAILURE);
	}
	return val;
}

/*
 * stress_set_mbind()
 * @arg: list of NUMA nodes to bind to, comma separated
 *
 * Returns: 0 - OK
 */
int stress_set_mbind(const char *arg)
{
	char *str, *ptr, *token;
	unsigned long max_node;
	unsigned long *nodemask;
	const size_t nodemask_bits = sizeof(*nodemask) * 8;
	size_t nodemask_sz;

	if (stress_numa_count_mem_nodes(&max_node) < 0) {
		(void)fprintf(stderr, "no NUMA nodes found, ignoring --mbind setting '%s'\n", arg);
		return 0;
	}

	nodemask_sz = (max_node + (nodemask_bits - 1)) / nodemask_bits;
	nodemask = calloc(nodemask_sz, sizeof(*nodemask));
	if (!nodemask) {
		(void)fprintf(stderr, "parsing --mbind: cannot allocate NUMA nodemask, out of memory\n");
		_exit(EXIT_FAILURE);
	}

	str = stress_const_optdup(arg);
	if (!str) {
		(void)fprintf(stderr, "out of memory duplicating argument '%s'\n", arg);
		_exit(EXIT_FAILURE);
	}

	for (ptr = str; (token = strtok(ptr, ",")) != NULL; ptr = NULL) {
		unsigned long i, lo, hi;
		char *tmpptr = strstr(token, "-");

		hi = lo = stress_parse_node(token);
		if (tmpptr) {
			tmpptr++;
			if (*tmpptr)
				hi = stress_parse_node(tmpptr);
			else {
				(void)fprintf(stderr, "%s: expecting number following "
					"'-' in '%s'\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
			if (hi <= lo) {
				(void)fprintf(stderr, "%s: invalid range in '%s' "
					"(end value must be larger than "
					"start value\n", option, token);
				free(str);
				_exit(EXIT_FAILURE);
			}
		}
		stress_check_numa_range(max_node, lo);
		stress_check_numa_range(max_node, hi);

		for (i = lo; i <= hi; i++) {
			STRESS_SETBIT(nodemask, i);
			if (shim_set_mempolicy(MPOL_BIND, nodemask, max_node) < 0) {
				(void)fprintf(stderr, "%s: could not set NUMA memory policy for node %lu, errno=%d (%s)\n",
					option, i, errno, strerror(errno));
				free(str);
				_exit(EXIT_FAILURE);
			}
		}
	}

	free(nodemask);
	free(str);
	return 0;
}

#else
int stress_numa_nodes(void)
{
	return 1;
}

int stress_numa_count_mem_nodes(unsigned long *max_node)
{
	*max_node = 0;

	return -1;
}

int stress_set_mbind(const char *arg)
{
	(void)arg;

	(void)fprintf(stderr, "%s: setting NUMA memory policy binding not supported\n", option);
	_exit(EXIT_FAILURE);
}
#endif
