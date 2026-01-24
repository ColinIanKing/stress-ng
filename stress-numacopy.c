/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-numa.h"

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#define STRESS_NUMACOPY_LOOPS	(100)
#define NUMA_NODES_MAX 		(64L)

static const stress_help_t help[] = {
	{ NULL,	"numa N",		"start N workers stressing NUMA interfaces" },
	{ NULL,	"numa-ops N",		"stop after N NUMA bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if defined(__NR_mbind)

/*
 *  stress_numacopy()
 *	stress copying data between NUMA nodes
 */
static int stress_numacopy(stress_args_t *args)
{
	stress_numa_mask_t *numa_mask, *numa_nodes;
	const size_t page_size = args->page_size;
	size_t numa_bytes, numa_pages_size;
	int rc = EXIT_FAILURE;
	uint8_t **numa_pages, *local_page;
	long int i, node, num_numa_nodes, num_numa_nodes_squared;
	double numa_pages_memcpy = 0.0, numa_pages_memset = 0.0;
	double rate, duration= 0;
	double *numa_duration;

	numa_nodes = stress_numa_mask_alloc();
	if (!numa_nodes) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto deinit;
	}
	num_numa_nodes = stress_numa_mask_nodes_get(numa_nodes);
	if (num_numa_nodes < 1) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto numa_nodes_free;
	}
	numa_mask = stress_numa_mask_alloc();
	if (!numa_mask) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto numa_nodes_free;
	}

	if (num_numa_nodes > NUMA_NODES_MAX) {
		if (stress_instance_zero(args))
			pr_inf("%s: too many NUMA nodes, using just %zd of %zd NUMA nodes\n", args->name, NUMA_NODES_MAX, num_numa_nodes);
		num_numa_nodes = NUMA_NODES_MAX;
		num_numa_nodes_squared = NUMA_NODES_MAX * NUMA_NODES_MAX;
	} else {
		if (stress_instance_zero(args))
			pr_inf("%s: using %zd NUMA nodes\n", args->name, num_numa_nodes);
		num_numa_nodes_squared = num_numa_nodes * num_numa_nodes;
	}

	numa_duration = (double *)calloc(num_numa_nodes_squared, sizeof(*numa_duration));
	if (!numa_duration) {
		pr_inf_skip("%s: failed to allocate numa duration array, skipping strssor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto numa_mask_free;
	}
	for (i = 0; i < num_numa_nodes_squared; i++)
		numa_duration[i] = 0.0;


	numa_pages_size = (size_t)num_numa_nodes * sizeof(*numa_pages);
	numa_bytes = (args->page_size * (size_t)(num_numa_nodes + 1)) +
		((numa_pages_size + page_size - 1) & ~(page_size - 1));
	if (stress_instance_zero(args))
		stress_usage_bytes(args, numa_bytes, numa_bytes * args->instances);

	numa_pages = (uint8_t **)stress_mmap_populate(NULL, numa_pages_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (numa_pages == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap pages array of %ld elements%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, num_numa_nodes,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto numa_duration_free;
	}
	stress_set_vma_anon_name(numa_pages, numa_pages_size, "pages");

	local_page = (uint8_t *)stress_mmap_populate(NULL, page_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (local_page == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap a local page%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto numa_pages_free;
	}
	stress_set_vma_anon_name(numa_pages, numa_pages_size, "pages");

	for (node = 0; node < num_numa_nodes; node++) {
		long lret;

		numa_pages[node] = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (numa_pages[node] == MAP_FAILED) {
			numa_pages[node] = NULL;
			goto local_page_free;
		}

		STRESS_SETBIT(numa_mask->mask, node);
		lret = shim_mbind(numa_pages[node], (unsigned long int)page_size, MPOL_BIND, numa_mask->mask,
				numa_mask->max_nodes, MPOL_MF_MOVE | MPOL_MF_STRICT);
		(void)shim_memset(numa_pages[node], 0xff, page_size);
		if (UNLIKELY(lret < 0)) {
			pr_fail("%s: mbind to node %ld using MPOL_MF_MOVE failed, errno=%d (%s)\n",
				args->name, node, errno, strerror(errno));
			goto err;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		long int node_from, node_to;

		node = 0;
		for (node_from = 0; node_from < num_numa_nodes; node_from++) {
			for (node_to = 0; node_to < num_numa_nodes; node_to++) {
				uint8_t val;
				int j;
				double t = stress_time_now(), dt;

				for (j = 0; j < STRESS_NUMACOPY_LOOPS; j++) {
					/* fill local page, copy to node_from, copy node_from to node_to */
					val = stress_mwc8();
					(void)shim_memset(local_page, val, page_size);

					(void)shim_memcpy(numa_pages[node_from], local_page, page_size);
					(void)shim_memcpy(numa_pages[node_to], numa_pages[node_from], page_size);

					if (*numa_pages[node_to] != val) {
						pr_inf("%s: invalid value in node data %ld\n", args->name, node_to);
					}

					/* fill local page, copy to node_to, copy node_to to node_from */
					val = stress_mwc8();
					(void)shim_memset(local_page, val, page_size);

					(void)shim_memcpy(numa_pages[node_to], local_page, page_size);
					(void)shim_memcpy(numa_pages[node_from], numa_pages[node_to], page_size);

					if (*numa_pages[node_from] != val) {
						pr_inf("%s: invalid value in node data %ld\n", args->name, node_from);
					}
				}
				dt = stress_time_now() - t;
				duration += dt;
				numa_duration[node] += dt;
				node++;
			}
		}
		numa_pages_memset += 2.0 * (double)STRESS_NUMACOPY_LOOPS;
		numa_pages_memcpy += 4.0 * (double)STRESS_NUMACOPY_LOOPS;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	if (duration > 0.0) {
		long int node_from, node_to;
		char str[7 + (num_numa_nodes * 6)];
		char buf[7];

		pr_block_begin();
		pr_inf("percentage of run time copying data node per node:\n");
		*str = '\0';
		for (node = 0; node < num_numa_nodes; node++) {

			snprintf(buf, sizeof(buf), " %5.0f", (double)node);
			shim_strlcat(str, buf, sizeof(str));
		}
		pr_inf("node %s\n", str);
		node = 0;
		for (node_from = 0; node_from < num_numa_nodes; node_from++) {
			snprintf(str, sizeof(str), "%5.0f", (double)node_from);

			for (node_to = 0; node_to < num_numa_nodes; node_to++) {
				snprintf(buf, sizeof(buf), " %5.2f", 100.0 * numa_duration[node] / duration);
				shim_strlcat(str, buf, sizeof(str));
				node++;
			}
			pr_inf("%s\n", str);
		}
		pr_block_end();
	}

	numa_pages_memset *= (double)num_numa_nodes_squared;
	numa_pages_memcpy *= (double)num_numa_nodes_squared;

	rate = duration > 0.0 ? numa_pages_memset / duration : 0.0;
	stress_metrics_set(args, 1, "numa_pages filled per sec", rate, STRESS_METRIC_GEOMETRIC_MEAN);
	rate = duration > 0.0 ? numa_pages_memcpy / duration : 0.0;
	stress_metrics_set(args, 0, "pages copied per sec", rate, STRESS_METRIC_GEOMETRIC_MEAN);


	rc = EXIT_SUCCESS;
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

local_page_free:
	(void)munmap((void *)local_page, page_size);
numa_pages_free:
	for (node = 0; node < num_numa_nodes; node++) {
		if (numa_pages[node])
			(void)munmap((void *)numa_pages[node], page_size);
	}
	(void)munmap((void *)numa_pages, numa_pages_size);
numa_duration_free:
	free(numa_duration);
numa_mask_free:
	stress_numa_mask_free(numa_mask);
numa_nodes_free:
	stress_numa_mask_free(numa_nodes);
deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_numacopy_info = {
	.stressor = stress_numacopy,
	.classifier = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_numacopy_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without linux/mempolicy.h, mbind()"
};
#endif
