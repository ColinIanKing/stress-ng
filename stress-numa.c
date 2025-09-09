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
#include "core-builtin.h"
#include "core-capabilities.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-numa.h"

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#define MIN_NUMA_MMAP_BYTES	(1 * MB)
#define MAX_NUMA_MMAP_BYTES	(MAX_MEM_LIMIT)
#define DEFAULT_NUMA_MMAP_BYTES	(4 * MB)

static const stress_help_t help[] = {
	{ NULL,	"numa N",		"start N workers stressing NUMA interfaces" },
	{ NULL,	"numa-bytes N",		"size of memory region to be exercised" },
	{ NULL,	"numa-ops N",		"stop after N NUMA bogo operations" },
	{ NULL,	"numa-shuffle-addr",	"shuffle page addresses to move to numa nodes" },
	{ NULL,	"numa-shuffle-node",	"shuffle numa nodes on numa pages moves" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_numa_bytes,        "numa-bytes",        TYPE_ID_SIZE_T_BYTES_VM, MIN_NUMA_MMAP_BYTES, MAX_NUMA_MMAP_BYTES, NULL },
	{ OPT_numa_shuffle_addr, "numa-shuffle-addr", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_numa_shuffle_node, "numa-shuffle-node", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__NR_get_mempolicy) &&	\
    defined(__NR_mbind) &&		\
    defined(__NR_migrate_pages) &&	\
    defined(__NR_move_pages) &&		\
    defined(__NR_set_mempolicy)

#define STRESS_NUMA_STAT_NUMA_HIT	(0)
#define STRESS_NUMA_STAT_NUMA_MISS	(1)
#define STRESS_NUMA_STAT_MAX		(2)

typedef struct {
	uint64_t value[STRESS_NUMA_STAT_MAX];
} stress_numa_stats_t;

static void stress_numa_stats_read(stress_numa_stats_t *stats)
{
	DIR *dir;
	const struct dirent *d;
	static const char *path = "/sys/devices/system/node";
	static const struct {
		const char *name;
		const size_t len;
		size_t idx;
	} numa_fields[] = {
		{ "numa_hit",	8,	STRESS_NUMA_STAT_NUMA_HIT },
		{ "numa_miss",	9,	STRESS_NUMA_STAT_NUMA_MISS },
	};

	(void)shim_memset(stats, 0, sizeof(*stats));

	dir = opendir(path);
	if (!dir)
		return;

	while ((d = readdir(dir)) != NULL) {
		char filename[PATH_MAX];
		char buffer[256];
		FILE *fp;

		if (shim_dirent_type(path, d) != SHIM_DT_DIR)
			continue;
		if (strncmp(d->d_name, "node", 4))
			continue;

		(void)snprintf(filename, sizeof(filename), "%s/%s/numastat", path, d->d_name);
		fp = fopen(filename, "r");
		if (!fp)
			continue;

		while (fgets(buffer, sizeof(buffer), fp) != NULL) {
			size_t i;

			for (i = 0; i < SIZEOF_ARRAY(numa_fields); i++) {
				if (strncmp(buffer, numa_fields[i].name, numa_fields[i].len) == 0) {
					uint64_t val = 0;

					if (sscanf(buffer + numa_fields[i].len + 1, "%" SCNu64, &val) == 1) {
						const size_t idx = numa_fields[i].idx;

						stats->value[idx] += val;
					}
				}
			}
		}
		(void)fclose(fp);
	}
	(void)closedir(dir);
}

/*
 *  stress_numa_check_maps()
 *	scan process' numa_maps file to see if ptr is
 *	on the expected node and keep tally of total nodes
 *	checked and correct node matches
 */
static void stress_numa_check_maps(
	const void *ptr,
	const int expected_node,
	uint64_t *correct_nodes,
	uint64_t *total_nodes)
{
	FILE *fp;
	char buffer[1024];

	fp = fopen("/proc/self/numa_maps", "r");
	if (!fp)
		return;

	while (fgets(buffer, sizeof(buffer), fp) != NULL) {
		int n;
		uintptr_t addr;

		n = sscanf(buffer, "%" SCNxPTR, &addr);
		if ((n == 1) && (ptr == (void *)addr)) {
			/* find active= field */
			const char *str = strstr(buffer, "active=");

			if (str) {
				int node;

				/* skip to Nx field, read node number */
				while (*str && *str != ' ')
					str++;
				while (*str == ' ')
					str++;
				if (*str== 'N') {
					str++;
					if ((sscanf(str, "%d", &node) == 1) &&
					    (expected_node == node)) {
						(*correct_nodes)++;
						break;
					}
				}
			}
			break;
		}
	}
	(*total_nodes)++;
	(void)fclose(fp);
}

/*
 *  stress_numa()
 *	stress the Linux NUMA interfaces
 */
static int stress_numa(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t num_pages, numa_bytes, numa_bytes_total = DEFAULT_NUMA_MMAP_BYTES;

	uint8_t *buf;
	int rc = EXIT_FAILURE;
	const bool cap_sys_nice = stress_check_capability(SHIM_CAP_SYS_NICE);
	int *status, *dest_nodes;
	int failed = 0;
	void **pages;
	size_t k;
	bool numa_shuffle_addr = false, numa_shuffle_node = false;
	stress_numa_stats_t stats_begin, stats_end;
	size_t status_size, dest_nodes_size, pages_size;
	double t, duration, metric;
	uint64_t correct_nodes = 0, total_nodes = 0;
	stress_numa_mask_t *numa_mask, *old_numa_mask, *numa_nodes;
	unsigned long int node;

	if (!stress_get_setting("numa-bytes", &numa_bytes_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			numa_bytes_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			numa_bytes_total = MIN_NUMA_MMAP_BYTES;
	}
	if (!stress_get_setting("numa-shuffle-addr", &numa_shuffle_addr)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			numa_shuffle_addr = true;
	}
	if (!stress_get_setting("numa-shuffle-node", &numa_shuffle_node)) {
		if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
			numa_shuffle_node = true;
	}

	numa_bytes = numa_bytes_total / args->instances;
	numa_bytes &= ~(page_size - 1);
	if (numa_bytes < MIN_NUMA_MMAP_BYTES)
		numa_bytes = MIN_NUMA_MMAP_BYTES;
	if (stress_instance_zero(args))
		stress_usage_bytes(args, numa_bytes, numa_bytes_total);

	num_pages = numa_bytes / page_size;

	numa_nodes = stress_numa_mask_alloc();
	if (!numa_nodes) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto deinit;
	}
	if (stress_numa_mask_nodes_get(numa_nodes) < 1) {
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
	old_numa_mask = stress_numa_mask_alloc();
	if (!old_numa_mask) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto numa_mask_free;
	}

	if (!args->instance) {
		char str[32];

		stress_uint64_to_str(str, sizeof(str), (uint64_t)numa_bytes, 1, true);
		pr_inf("%s: system has %lu of a maximum %lu memory NUMA nodes. Using %s mappings for each instance.\n",
			args->name, numa_mask->nodes, numa_mask->max_nodes, str);
	}

	status_size = num_pages * sizeof(*status);
	status = (int *)stress_mmap_populate(NULL, status_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (status == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap status array of %zu elements%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, num_pages,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto old_numa_mask_free;
	}
	stress_set_vma_anon_name(status, status_size, "status");

	dest_nodes_size = num_pages * sizeof(*dest_nodes);
	dest_nodes = (int *)stress_mmap_populate(NULL, dest_nodes_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (dest_nodes == MAP_FAILED) {
		pr_inf("%s: failed to mmap dest_nodes array of %zu elements%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, num_pages,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto status_free;
	}
	stress_set_vma_anon_name(dest_nodes, dest_nodes_size, "dest-nodes");

	pages_size = num_pages * sizeof(*pages);
	pages = (void **)stress_mmap_populate(NULL, pages_size,
					PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (pages == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap pages array of %zu elements%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, num_pages,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto dest_nodes_free;
	}
	stress_set_vma_anon_name(pages, pages_size, "pages");

	/*
	 *  We need a buffer to migrate around NUMA nodes
	 */
	buf = stress_mmap_populate(NULL, numa_bytes,
		PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, 0, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap a region of %zu bytes%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, numa_bytes,
			stress_get_memfree_str(), errno, strerror(errno));
		rc = EXIT_NO_RESOURCE;
		goto pages_free;
	}
	stress_set_vma_anon_name(buf, numa_bytes, "numa-shared-data");
	(void)stress_madvise_mergeable(buf, numa_bytes);
	(void)stress_madvise_nohugepage(buf, numa_bytes);

	stress_numa_stats_read(&stats_begin);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	node = stress_numa_next_node(0, numa_nodes);

	k = 0;
	t = stress_time_now();
	do {
		int j, mode, ret;
		long int lret;
		unsigned long int i;
		uint8_t *ptr;
		unsigned cpu, curr_node;
		struct shim_getcpu_cache cache;

		(void)shim_memset(numa_mask->mask, 0x00, numa_mask->mask_size);

		/*
		 *  Fetch memory policy
		 */
		ret = shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes,
					 buf, MPOL_F_ADDR);
		if (UNLIKELY(ret < 0)) {
			if (errno == EPERM) {
				pr_inf_skip("%s: get_mempolicy, no permission, skipping stressor\n",
					args->name);
				rc = EXIT_NO_RESOURCE;
				goto err;
			} else if (errno != ENOSYS) {
				pr_fail("%s: get_mempolicy failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		}

		/* Exercise invalid numa mask */
		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, 0, buf, MPOL_F_NODE));

		/* Exercise invalid flag */
		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes, buf, ~0UL));

		/* Exercise invalid NULL addr condition */
		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes, NULL, MPOL_F_ADDR));

		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes, buf, MPOL_F_NODE));

		/* Exercise MPOL_F_MEMS_ALLOWED flag syscalls */
		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes, buf, MPOL_F_MEMS_ALLOWED));

		VOID_RET(int, shim_get_mempolicy(&mode, numa_mask->mask, numa_mask->max_nodes, buf, MPOL_F_MEMS_ALLOWED | MPOL_F_NODE));

		if (UNLIKELY(!stress_continue_flag()))
			break;

		ret = shim_set_mempolicy(MPOL_PREFERRED, NULL, 0);
		if (UNLIKELY(ret < 0)) {
			if (errno != ENOSYS) {
				pr_fail("%s: set_mempolicy failed, errno=%d (%s)\n",
					args->name, errno, strerror(errno));
				goto err;
			}
		}

		(void)stress_mmap_set_light(buf, numa_bytes, page_size);
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* Create a mix of _NONES options, invalid ones too */
		mode = 0;
#if defined(MPOL_F_STATIC_NODES)
		if (stress_mwc1())
			mode |= MPOL_F_STATIC_NODES;
#endif
#if defined(MPOL_F_RELATIVE_NODES)
		if (stress_mwc1())
			mode |= MPOL_F_RELATIVE_NODES;
#endif

		switch (stress_mwc8modn(12)) {
		case 0:
#if defined(MPOL_DEFAULT)
			VOID_RET(long int, shim_set_mempolicy(MPOL_DEFAULT | mode, NULL, numa_mask->max_nodes));
			break;
#endif
		case 1:
#if defined(MPOL_BIND)
			VOID_RET(long int, shim_set_mempolicy(MPOL_BIND | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 2:
#if defined(MPOL_INTERLEAVE)
			VOID_RET(long int, shim_set_mempolicy(MPOL_INTERLEAVE | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 3:
#if defined(MPOL_PREFERRED)
			VOID_RET(long int, shim_set_mempolicy(MPOL_PREFERRED | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 4:
#if defined(MPOL_LOCAL)
			VOID_RET(long int, shim_set_mempolicy(MPOL_LOCAL | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 5:
#if defined(MPOL_PREFERRED_MANY)
			VOID_RET(long int, shim_set_mempolicy(MPOL_PREFERRED_MANY | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 6:
#if defined(MPOL_WEIGHTED_INTERLEAVE)
			VOID_RET(long int, shim_set_mempolicy(MPOL_WEIGHTED_INTERLEAVE | mode, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		case 7:
			VOID_RET(long int, shim_set_mempolicy(0, numa_mask->mask, numa_mask->max_nodes));
			break;
		case 8:
			VOID_RET(long int, shim_set_mempolicy(mode, numa_mask->mask, numa_mask->max_nodes));
			break;
		case 9:
			/* Invalid mode */
			VOID_RET(long int, shim_set_mempolicy(mode | MPOL_F_STATIC_NODES | MPOL_F_RELATIVE_NODES, numa_mask->mask, numa_mask->max_nodes));
			break;
#if defined(MPOL_F_NUMA_BALANCING) &&	\
    defined(MPOL_LOCAL)
		case 10:
			/* Invalid  MPOL_F_NUMA_BALANCING | MPOL_LOCAL */
			VOID_RET(long int, shim_set_mempolicy(MPOL_F_NUMA_BALANCING | MPOL_LOCAL, numa_mask->mask, numa_mask->max_nodes));
			break;
#endif
		default:
			/* Intentionally invalid mode */
			VOID_RET(long int, shim_set_mempolicy(~0, numa_mask->mask, numa_mask->max_nodes));
		}

		/*
		 *  Fetch CPU and node, we just waste some cycles
		 *  doing this for stress reasons only
		 */
		(void)shim_getcpu(&cpu, &curr_node, NULL);

		/* Initialised cache to be safe */
		shim_memset(&cache, 0x00, sizeof(cache));

		/*
		 * tcache argument is unused in getcpu currently.
		 * Exercise getcpu syscall with non-null tcache
		 * pointer to ensure kernel doesn't break even
		 * when this argument is used in future.
		 */
		(void)shim_getcpu(&cpu, &curr_node, &cache);

		/*
		 *  mbind the buffer, first try MPOL_MF_STRICT which
		 *  may fail with EIO, so retry with MPOL_MF_MOVE
		 */
		shim_memset(numa_mask->mask, 0x00, numa_mask->mask_size);
		STRESS_SETBIT(numa_mask->mask, node);
		lret = shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, MPOL_MF_STRICT);
		if ((lret < 0) && (errno == EIO))
			lret = shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
				numa_mask->max_nodes, MPOL_MF_MOVE);
		if (UNLIKELY(lret < 0)) {
			if ((errno != EIO) && (errno != ENOSYS)) {
				pr_fail("%s: mbind to node %lu using MPOL_MF_MOVE failed, errno=%d (%s)\n",
					args->name, node, errno, strerror(errno));
				goto err;
			}
		} else {
			(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)numa_bytes, node, 0);
			(void)stress_mmap_set_light(buf, numa_bytes, page_size);
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/*
		 *  Exercise set_mempolicy_home_node
		 */
		(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)numa_bytes, numa_mask->nodes - 1, 0);
		(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)numa_bytes, 1, 0);
		(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)0, node, 0);
		(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)numa_bytes, node, 0);

		/*
		 *  mbind the buffer, now try MPOL_DEFAULT
		 */
		(void)shim_memset(numa_mask->mask, 0x00, numa_mask->mask_size);
		STRESS_SETBIT(numa_mask->mask, node);
		lret = shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, MPOL_DEFAULT);
		if (UNLIKELY(lret < 0)) {
			if ((errno != EIO) && (errno != ENOSYS)) {
				pr_fail("%s: mbind to node %lu using MPOL_DEFAULT failed, errno=%d (%s)\n",
					args->name, node, errno, strerror(errno));
				goto err;
			}
		} else {
			(void)shim_set_mempolicy_home_node((unsigned long int)buf,
				(unsigned long int)numa_bytes, node, 0);
			(void)stress_mmap_set_light(buf, numa_bytes, page_size);
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;

		/* Exercise invalid start address */
		VOID_RET(long int, shim_mbind((void *)(buf + 7), numa_bytes, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, MPOL_MF_MOVE));

		/* Exercise wrap around */
		VOID_RET(long int, shim_mbind((void *)(~(uintptr_t)0 & ~(page_size - 1)), page_size * 2,
			MPOL_BIND, numa_mask->mask, numa_mask->max_nodes, MPOL_MF_MOVE));

		/* Exercise invalid length */
		VOID_RET(long int, shim_mbind((void *)buf, ~0UL, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, MPOL_MF_MOVE));

		/* Exercise zero length, allowed, but is a no-op */
		VOID_RET(long int, shim_mbind((void *)buf, 0, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, MPOL_MF_MOVE));

		/* Exercise invalid nodes */
		VOID_RET(long int, shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
			0, MPOL_MF_MOVE));
		VOID_RET(long int, shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
			0xffffffff, MPOL_MF_MOVE));

		/* Exercise invalid flags */
		VOID_RET(long int, shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
			numa_mask->max_nodes, ~0U));

		/* Check mbind syscall cannot succeed without capability */
		if (!cap_sys_nice) {
			lret = shim_mbind((void *)buf, numa_bytes, MPOL_BIND, numa_mask->mask,
				numa_mask->max_nodes, MPOL_MF_MOVE_ALL);
			if (lret >= 0) {
				pr_fail("%s: mbind on node %lu using MPOL_MF_MOVE_ALL without capability CAP_SYS_NICE unexpectedly succeeded, "
						"errno=%d (%s)\n", args->name, node, errno, strerror(errno));
			}
		}

		/* Move to next node */
		node = stress_numa_next_node(node, numa_nodes);

		/*
		 *  Migrate all this processes pages to the current new node
		 */
		(void)shim_memset(old_numa_mask->mask, 0xff, old_numa_mask->mask_size);
		(void)shim_memset(numa_mask->mask, 0x00, numa_mask->mask_size);
		STRESS_SETBIT(numa_mask->mask, node);

		/*
	 	 *  Ignore any failures, this is not strictly important
		 */
		VOID_RET(long int, shim_migrate_pages(args->pid, numa_mask->max_nodes,
			old_numa_mask->mask, numa_mask->mask));

		/*
		 *  Exercise illegal pid
		 */
		VOID_RET(long int, shim_migrate_pages(~0, numa_mask->max_nodes, old_numa_mask->mask, numa_mask->mask));

		/*
		 *  Exercise illegal nodes
		 */
		VOID_RET(long int, shim_migrate_pages(args->pid, ~0UL, old_numa_mask->mask, numa_mask->mask));
		VOID_RET(long int, shim_migrate_pages(args->pid, 0, old_numa_mask->mask, numa_mask->mask));

		if (UNLIKELY(!stress_continue_flag()))
			break;

		for (j = 0; j < 16; j++) {
			int dest_node_of_buf = -1;

			/*
			 *  Now move pages to lots of different numa nodes
			 */
			for (ptr = buf, i = 0; i < num_pages; i++, ptr += page_size) {
				pages[k] = ptr;
				dest_nodes[k] = (int)node;
				k++;
				if (k >= num_pages)
					k = 0;
			}
			if (numa_shuffle_addr) {
				for (i = 0; i < num_pages; i++) {
					register const size_t l = stress_mwc32modn(num_pages);
					register void *tmp;

					tmp = pages[i];
					pages[i] = pages[l];
					pages[l] = tmp;
				}
			}
			if (numa_shuffle_node) {
				for (i = 0; i < num_pages; i++) {
					register const size_t l = stress_mwc32modn(num_pages);
					register int tmp;

					tmp = dest_nodes[i];
					dest_nodes[i] = dest_nodes[l];
					dest_nodes[l] = tmp;
				}
			}

			/* Touch each page */
			for (i = 0; i < num_pages; i++) {
				*(uintptr_t *)pages[i] = (uintptr_t)pages[i];
				if (pages[i] == buf)
					dest_node_of_buf = dest_nodes[i];
			}

			/*
			 *  ..and bump k to ensure next round the pages get reassigned to
			 *  a different node
			 */
			k++;
			if (k >= num_pages)
				k = 0;

			(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
			lret = shim_move_pages(args->pid, num_pages, pages,
				dest_nodes, status, MPOL_MF_MOVE);
			if (UNLIKELY(lret < 0)) {
				if (errno != ENOSYS) {
					pr_fail("%s: move_pages failed, errno=%d (%s)\n",
						args->name, errno, strerror(errno));
					goto err;
				}
			}

			stress_numa_check_maps(buf, dest_node_of_buf, &correct_nodes, &total_nodes);

			/* Check each page */
			for (i = 0; i < num_pages; i++) {
				if (*(uintptr_t *)pages[i] != (uintptr_t)pages[i]) {
					pr_fail("%s: data mismatch on page #%lu %p: expected %p, got %p\n",
						args->name, i, pages[i],
						(void *)pages[i], (void *)*(uintptr_t *)pages[i]);
					failed++;
					if (failed > 32) {
						pr_inf("%s: aborting due to too many failures\n", args->name);
						goto err;
					}
				}
			}
			(void)stress_mmap_set_light(buf, numa_bytes, page_size);
			if (UNLIKELY(!stress_continue_flag()))
				break;
		}

#if defined(MPOL_MF_MOVE_ALL)
		/* Exercise MPOL_MF_MOVE_ALL, this needs privilege, ignore failure */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(args->pid, num_pages, pages, dest_nodes, status, MPOL_MF_MOVE_ALL));
#endif

		/* Exercise invalid pid on move pages */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(~0, 1, pages, dest_nodes, status, MPOL_MF_MOVE));

		/* Exercise 0 nr_pages */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(args->pid, 0, pages, dest_nodes, status, MPOL_MF_MOVE));

		/* Exercise invalid move flags */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(args->pid, 1, pages, dest_nodes, status, ~0));

		/* Exercise zero flag, should succeed */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(args->pid, 1, pages, dest_nodes, status, 0));

		/* Exercise invalid address */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = (void *)(~(uintptr_t)0 & ~(args->page_size - 1));
		VOID_RET(long int, shim_move_pages(args->pid, 1, pages, dest_nodes, status, MPOL_MF_MOVE));

		/* Exercise invalid dest_node */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		dest_nodes[0] = ~0;
		VOID_RET(long int, shim_move_pages(args->pid, 1, pages, dest_nodes, status, MPOL_MF_MOVE));

		/* Exercise NULL nodes */
		(void)shim_memset(status, 0x00, num_pages * sizeof(*status));
		pages[0] = buf;
		VOID_RET(long int, shim_move_pages(args->pid, 1, pages, NULL, status, MPOL_MF_MOVE));

		stress_bogo_inc(args);
	} while (stress_continue(args));

	duration = stress_time_now() - t;
	stress_numa_stats_read(&stats_end);

	metric = (duration > 0) ? ((double)stats_end.value[STRESS_NUMA_STAT_NUMA_HIT] -
				 (double)stats_begin.value[STRESS_NUMA_STAT_NUMA_HIT]) / duration : 0.0;
	stress_metrics_set(args, 0, "NUMA hits per sec", metric, STRESS_METRIC_GEOMETRIC_MEAN);

	metric = (duration > 0) ? ((double)stats_end.value[STRESS_NUMA_STAT_NUMA_MISS] -
				 (double)stats_begin.value[STRESS_NUMA_STAT_NUMA_MISS]) / duration : 0.0;
	stress_metrics_set(args, 1, "NUMA misses per sec", metric, STRESS_METRIC_GEOMETRIC_MEAN);

	/* total_nodes may be zero if we can't read /proc/self/numa_maps */
	if (total_nodes > 0) {
		metric = 100.0 * (double)correct_nodes / (double)total_nodes;
		stress_metrics_set(args, 2, "% of checked pages on specified NUMA node", metric, STRESS_METRIC_GEOMETRIC_MEAN);
	}

	rc = EXIT_SUCCESS;
err:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)buf, numa_bytes);

pages_free:
	(void)munmap((void *)pages, pages_size);
dest_nodes_free:
	(void)munmap((void *)dest_nodes, dest_nodes_size);
status_free:
	(void)munmap((void *)status, status_size);
old_numa_mask_free:
	stress_numa_mask_free(old_numa_mask);
numa_mask_free:
	stress_numa_mask_free(numa_mask);
numa_nodes_free:
	stress_numa_mask_free(numa_nodes);
deinit:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_numa_info = {
	.stressor = stress_numa,
	.classifier = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_numa_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without linux/mempolicy.h, get_mempolicy(), mbind(), migrate_pages(), move_pages() or set_mempolicy()"
};
#endif
