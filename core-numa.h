/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#ifndef CORE_NUMA_H
#define CORE_NUMA_H

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#define BITS_PER_BYTE		(8)
#define NUMA_LONG_BITS		(sizeof(unsigned long int) * BITS_PER_BYTE)

/* numa mode mask */
typedef struct stress_numa_mask {
	unsigned long int nodes;		/* NUMA nodes in system */
	unsigned long int max_nodes;		/* Max NUMA nodes (kernel config) */
	unsigned long int *mask;		/* NUMA node mask */
	size_t mask_size;			/* Allocated mask size in bytes */
	size_t numa_elements;			/* NUMA node mask size on 64 bit ints */
} stress_numa_mask_t;

#if !defined(MPOL_DEFAULT)
#define MPOL_DEFAULT		(0)
#endif
#if !defined(MPOL_PREFERRED)
#define MPOL_PREFERRED		(1)
#endif
#if !defined(MPOL_BIND)
#define MPOL_BIND		(2)
#endif
#if !defined(MPOL_INTERLEAVE)
#define MPOL_INTERLEAVE		(3)
#endif
#if !defined(MPOL_LOCAL)
#define MPOL_LOCAL		(4)
#endif
#if !defined(MPOL_PREFERRED_MANY)
#define MPOL_PREFERRED_MANY	(5)
#endif
#if !defined(MPOL_WEIGHTED_INTERLEAVE)
#define MPOL_WEIGHTED_INTERLEAVE (6)
#endif

#if !defined(MPOL_F_NODE)
#define MPOL_F_NODE		(1 << 0)
#endif
#if !defined(MPOL_F_ADDR)
#define MPOL_F_ADDR		(1 << 1)
#endif
#if !defined(MPOL_F_MEMS_ALLOWED)
#define MPOL_F_MEMS_ALLOWED	(1 << 2)
#endif

#if !defined(MPOL_MF_STRICT)
#define MPOL_MF_STRICT		(1 << 0)
#endif
#if !defined(MPOL_MF_MOVE)
#define MPOL_MF_MOVE		(1 << 1)
#endif
#if !defined(MPOL_MF_MOVE_ALL)
#define MPOL_MF_MOVE_ALL	(1 << 2)
#endif

#if !defined(MPOL_F_NUMA_BALANCING)
#define MPOL_F_NUMA_BALANCING	(1 << 13)
#endif
#if !defined(MPOL_F_RELATIVE_NODES)
#define MPOL_F_RELATIVE_NODES	(1 << 14)
#endif
#if !defined(MPOL_F_STATIC_NODES)
#define MPOL_F_STATIC_NODES	(1 << 15)
#endif

extern unsigned long int stress_numa_count_mem_nodes(unsigned long int *max_node);
extern unsigned long int stress_numa_mask_nodes_get(stress_numa_mask_t *numa_mask);
extern unsigned long stress_numa_next_node(const unsigned long int node,
	stress_numa_mask_t *numa_nodes);
extern unsigned long int stress_numa_nodes(void);
extern int stress_set_mbind(const char *arg);
extern stress_numa_mask_t *stress_numa_mask_alloc(void);
extern void stress_numa_mask_and_node_alloc(stress_args_t *args,
	stress_numa_mask_t **numa_nodes, stress_numa_mask_t **numa_mask,
	const char *option, bool *flag);
extern void stress_numa_mask_free(stress_numa_mask_t *mask);
extern void stress_numa_randomize_pages(stress_args_t *args,
	stress_numa_mask_t *numa_nodes, stress_numa_mask_t *numa_mask,
	void *buffer, const size_t buffer_size, const size_t page_size);
#endif
