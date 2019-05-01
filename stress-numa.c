/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"numa N",	"start N workers stressing NUMA interfaces" },
	{ NULL,	"numa-ops N",	"stop after N NUMA bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(__NR_get_mempolicy) &&	\
    defined(__NR_mbind) &&		\
    defined(__NR_migrate_pages) &&	\
    defined(__NR_move_pages) &&		\
    defined(__NR_set_mempolicy)

#define BITS_PER_BYTE		(8)
#define NUMA_LONG_BITS		(sizeof(unsigned long) * BITS_PER_BYTE)

#define MPOL_DEFAULT		(0)
#define MPOL_PREFERRED		(1)
#define MPOL_BIND		(2)
#define MPOL_INTERLEAVE		(3)
#define MPOL_LOCAL		(4)

#define MPOL_F_NODE		(1 << 0)
#define MPOL_F_ADDR		(1 << 1)
#define MPOL_F_MEMS_ALLOWED	(1 << 2)

#define MPOL_MF_STRICT		(1 << 0)
#define MPOL_MF_MOVE		(1 << 1)
#define MPOL_MF_MOVE_ALL	(1 << 2)

#define MMAP_SZ			(4 * MB)

typedef struct node {
	struct node	*next;
	uint32_t	node_id;
} node_t;

/*
 *  stress_numa_free_nodes()
 *	free circular list of node info
 */
static void stress_numa_free_nodes(node_t *nodes)
{
	node_t *n = nodes;

	while (n) {
		node_t *next = n->next;

		free(n);
		n = next;

		if (n == nodes)
			break;
	}
}

/*
 *  hex_to_int()
 *	convert ASCII hex digit to integer
 */
static inline int hex_to_int(const char ch)
{
	if (ch >= '0' && ch <= '9')
		return ch - '0';
	if (ch >= 'a' && ch <= 'f')
		return ch - 'a' + 10;
	if (ch >= 'A' && ch <= 'F')
		return ch - 'F' + 10;
	return -1;
}

/*
 *  stress_numa_get_mem_nodes(void)
 *	collect number of NUMA memory nodes, add them to a
 *	circular linked list - also, return maximum number
 *	of nodes
 */
static int stress_numa_get_mem_nodes(node_t **node_ptr,
				     unsigned long *max_nodes)
{
	FILE *fp;
	unsigned long n = 0, node_id = 0;
	node_t *tail = NULL;
	*node_ptr = NULL;
	char buffer[8192], *str = NULL, *ptr;

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
	while (*ptr != ' ' && (ptr > str)) {
		int val, i;

		/* Skip commas */
		if (*ptr == ',') {
			ptr--;
			continue;
		}

		val = hex_to_int(*ptr);
		if (val < 0)
			return -1;

		/* Each hex digit represent 4 memory nodes */
		for (i = 0; i < 4; i++) {
			if (val & (1 << i)) {
				node_t *node = calloc(1, sizeof(*node));
				if (!node)
					return -1;
				node->node_id = node_id;
				node->next = *node_ptr;
				*node_ptr = node;
				if (!tail)
					tail = node;
				tail->next = node;
				n++;
			}
			node_id++;
		}
		ptr--;
	}

	*max_nodes = node_id;
	return n;
}

/*
 *  stress_numa()
 *	stress the Linux NUMA interfaces
 */
static int stress_numa(const args_t *args)
{
	long numa_nodes;
	unsigned long max_nodes;
	const unsigned long lbits = NUMA_LONG_BITS;
	const unsigned long page_sz = args->page_size;
	const unsigned long num_pages = MMAP_SZ / page_sz;
	uint8_t *buf;
	node_t *n;
	int rc = EXIT_FAILURE;

	numa_nodes = stress_numa_get_mem_nodes(&n, &max_nodes);
	if (numa_nodes < 1) {
		pr_inf("%s: no NUMA nodes found, "
			"aborting test\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto numa_free;
	}
	if (!args->instance) {
		pr_inf("%s: system has %lu of a maximum %lu memory NUMA nodes\n",
			args->name, numa_nodes, max_nodes);
	}

	/*
	 *  We need a buffer to migrate around NUMA nodes
	 */
	buf = mmap(NULL, MMAP_SZ, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (buf == MAP_FAILED) {
		rc = exit_status(errno);
		pr_fail("%s: mmap'd region of %zu bytes failed",
			args->name, (size_t)MMAP_SZ);
		goto numa_free;
	}

	do {
		int j, mode, ret, status[num_pages], dest_nodes[num_pages];
		unsigned long i, node_mask[lbits], old_node_mask[lbits];
		unsigned long max_node_id_count;
		void *pages[num_pages];
		uint8_t *ptr;
		node_t *n_tmp;
		unsigned cpu, curr_node;

		/*
		 *  Fetch memory policy
		 */
		ret = shim_get_mempolicy(&mode, node_mask, max_nodes,
			(unsigned long)buf, MPOL_F_ADDR);
		if (ret < 0) {
			pr_fail_err("get_mempolicy");
			goto err;
		}
		if (!g_keep_stressing_flag)
			break;

		ret = shim_set_mempolicy(MPOL_PREFERRED, NULL, max_nodes);
		if (ret < 0) {
			pr_fail_err("set_mempolicy");
			goto err;
		}
		(void)memset(buf, 0xff, MMAP_SZ);
		if (!g_keep_stressing_flag)
			break;

		/*
		 *  Fetch CPU and node, we just waste some cycled
		 *  doing this for stress reasons only
		 */
		(void)shim_getcpu(&cpu, &curr_node, NULL);

		/*
		 *  mbind the buffer, first try MPOL_STRICT which
		 *  may fail with EIO
		 */
		(void)memset(node_mask, 0, sizeof(node_mask));
		STRESS_SETBIT(node_mask, n->node_id);
		ret = shim_mbind(buf, MMAP_SZ, MPOL_BIND, node_mask,
			max_nodes, MPOL_MF_STRICT);
		if (ret < 0) {
			if (errno != EIO) {
				pr_fail_err("mbind");
				goto err;
			}
		} else {
			(void)memset(buf, 0xaa, MMAP_SZ);
		}
		if (!g_keep_stressing_flag)
			break;

		/*
		 *  mbind the buffer, now try MPOL_DEFAULT
		 */
		(void)memset(node_mask, 0, sizeof(node_mask));
		STRESS_SETBIT(node_mask, n->node_id);
		ret = shim_mbind(buf, MMAP_SZ, MPOL_BIND, node_mask,
			max_nodes, MPOL_DEFAULT);
		if (ret < 0) {
			if (errno != EIO) {
				pr_fail_err("mbind");
				goto err;
			}
		} else {
			(void)memset(buf, 0x5c, MMAP_SZ);
		}
		if (!g_keep_stressing_flag)
			break;

		/* Move to next node */
		n = n->next;

		/*
		 *  Migrate all this processes pages to the current new node
		 */
		(void)memset(old_node_mask, 0, sizeof(old_node_mask));
		max_node_id_count = max_nodes;
		for (i = 0; max_node_id_count >= lbits && i < lbits; i++) {
			old_node_mask[i] = ULONG_MAX;
			max_node_id_count -= lbits;
		}
		if (i < lbits)
			old_node_mask[i] = (1UL << max_node_id_count) - 1;
		(void)memset(node_mask, 0, sizeof(node_mask));
		STRESS_SETBIT(node_mask, n->node_id);
		ret = shim_migrate_pages(args->pid, max_nodes,
			old_node_mask, node_mask);
		if (ret < 0) {
			pr_fail_err("migrate_pages");
			goto err;
		}
		if (!g_keep_stressing_flag)
			break;

		n_tmp = n;
		for (j = 0; j < 16; j++) {
			/*
			 *  Now move pages to lots of different numa nodes
			 */
			for (ptr = buf, i = 0; i < num_pages; i++, ptr += page_sz, n_tmp = n_tmp->next) {
				pages[i] = ptr;
				dest_nodes[i] = n_tmp->node_id;
			}
			(void)memset(status, 0, sizeof(status));
			ret = shim_move_pages(args->pid, num_pages, pages,
				dest_nodes, status, MPOL_MF_MOVE);
			if (ret < 0) {
				pr_fail_err("move_pages");
				goto err;
			}
			(void)memset(buf, j, MMAP_SZ);
			if (!g_keep_stressing_flag)
				break;
		}
		inc_counter(args);
	} while (keep_stressing());

	rc = EXIT_SUCCESS;
err:
	(void)munmap(buf, MMAP_SZ);
numa_free:
	stress_numa_free_nodes(n);

	return rc;
}

stressor_info_t stress_numa_info = {
	.stressor = stress_numa,
	.class = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_numa_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_MEMORY | CLASS_OS,
	.help = help
};
#endif
