/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright (C) 2023      Colin Ian King.
 *
 */
#ifndef CORE_NUMA_H
#define CORE_NUMA_H

extern int stress_numa_count_mem_nodes(unsigned long *max_node);
extern int stress_set_mbind(const char *arg);

#endif
