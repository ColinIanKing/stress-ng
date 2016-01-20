/*
 * Copyright (C) 2016 Intel, Ltd.
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

#ifndef _STRESS_NG_CACHE_H
#define _STRESS_NG_CACHE_H

#if defined(__linux__)

#define SYS_CPU_PREFIX               "/sys/devices/system/cpu"
#define GLOB_PATTERN SYS_CPU_PREFIX  "/cpu[0-9]*"
#define GLOB_PATTERN_INDEX_PREFIX    "/cache/index[0-9]*"
/* Generally, LLC */
#define DEFAULT_CACHE_LEVEL          3

typedef enum cache_type {
	CACHE_TYPE_UNKNOWN = 0,
	CACHE_TYPE_DATA,
	CACHE_TYPE_INSTRUCTION,
	CACHE_TYPE_UNIFIED,

} cache_type_t;

typedef struct cpu_cache {
	int                level;
	cache_type_t       type;
	size_t             size;      /* bytes */
	int                line_size; /* bytes */
	int                ways;
} cpu_cache_t;

struct generic_map {
	const char   *name;
	unsigned int  value;
};

typedef struct cpu {
	unsigned int   num;
	bool           online;
	unsigned int   cache_count;
	cpu_cache_t   *caches;
} cpu_t;

typedef struct cpus {
	unsigned int   count;
	cpu_t         *cpus;
} cpus_t;

cpus_t *get_all_cpu_cache_details(void);
cpu_cache_t *get_cpu_cache(const cpus_t *cpus, int cache_level);
void free_cpu_caches(cpus_t *cpus);

#endif /* __linux__ */

#endif /* _STRESS_NG_CACHE_H */
