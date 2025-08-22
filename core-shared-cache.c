/*
 * Copyright (C) 2014-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-shared-cache.h"

#define MEM_CACHE_SIZE	(2 * MB)

/*
 *  stress_cache_alloc()
 *	allocate shared cache buffer
 */
int stress_cache_alloc(const char *name)
{
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint16_t max_cache_level = 0, level;
	char cache_info[512];
	const int numa_nodes = stress_numa_nodes();

	cpu_caches = stress_cpu_cache_get_all_details();

	if (g_shared->mem_cache.size > 0)
		goto init_done;

	if (!cpu_caches) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache details\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}

	max_cache_level = stress_cpu_cache_get_max_level(cpu_caches);
	if (max_cache_level == 0) {
		if (stress_warn_once())
			pr_dbg("%s: using defaults, cannot determine cache level details\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}
	if (g_shared->mem_cache.level > max_cache_level) {
		if (stress_warn_once())
			pr_dbg("%s: using cache maximum level L%d\n", name,
				max_cache_level);
		g_shared->mem_cache.level = max_cache_level;
	}

	cache = stress_cpu_cache_get(cpu_caches, g_shared->mem_cache.level);
	if (!cache) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as no suitable "
				"cache found\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE * numa_nodes;
		goto init_done;
	}

	if (g_shared->mem_cache.ways > 0) {
		uint64_t way_size;

		if (g_shared->mem_cache.ways > cache->ways) {
			if (stress_warn_once())
				pr_inf("%s: cache way value too high - "
					"defaulting to %" PRIu32 " (the maximum)\n",
					name, cache->ways);
			g_shared->mem_cache.ways = cache->ways;
		}
		way_size = cache->size / cache->ways;

		/* only fill the specified number of cache ways */
		g_shared->mem_cache.size = way_size * g_shared->mem_cache.ways * numa_nodes;
	} else {
		/* fill the entire cache */
		g_shared->mem_cache.size = cache->size * numa_nodes;
	}

	if (!g_shared->mem_cache.size) {
		if (stress_warn_once())
			pr_dbg("%s: using built-in defaults as "
				"unable to determine cache size\n", name);
		g_shared->mem_cache.size = MEM_CACHE_SIZE;
	}

	(void)shim_memset(cache_info, 0, sizeof(cache_info));
	for (level = 1; level <= max_cache_level; level++) {
		size_t cache_size = 0, cache_line_size = 0;

		stress_cpu_cache_get_level_size(level, &cache_size, &cache_line_size);
		if ((cache_size > 0) && (cache_line_size > 0)) {
			char tmp[64];

			(void)snprintf(tmp, sizeof(tmp), "%sL%" PRIu16 ": %zuK",
				(level > 1) ? ", " : "", level, cache_size >> 10);
			shim_strlcat(cache_info, tmp, sizeof(cache_info));
		}
	}
	pr_dbg("CPU data cache: %s\n", cache_info);
init_done:

	stress_free_cpu_caches(cpu_caches);
	g_shared->mem_cache.buffer =
		(uint8_t *)stress_mmap_anon_shared(g_shared->mem_cache.size, PROT_READ | PROT_WRITE);
	if (g_shared->mem_cache.buffer == MAP_FAILED) {
		g_shared->mem_cache.buffer = NULL;
		pr_err("%s: failed to mmap shared cache buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	stress_set_vma_anon_name(g_shared->mem_cache.buffer, g_shared->mem_cache.size, "mem-cache");

	g_shared->cacheline.size = (size_t)STRESS_PROCS_MAX * sizeof(uint8_t) * 2;
	g_shared->cacheline.buffer =
		(uint8_t *)stress_mmap_anon_shared(g_shared->cacheline.size, PROT_READ | PROT_WRITE);
	if (g_shared->cacheline.buffer == MAP_FAILED) {
		g_shared->cacheline.buffer = NULL;
		pr_err("%s: failed to mmap cacheline buffer, errno=%d (%s)\n",
			name, errno, strerror(errno));
		return -1;
	}
	stress_set_vma_anon_name(g_shared->cacheline.buffer, g_shared->cacheline.size, "cacheline");
	if (stress_warn_once()) {
		if (numa_nodes > 1) {
			pr_dbg("%s: shared cache buffer size: %" PRIu64 "K (LLC size x %d NUMA nodes)\n",
				name, g_shared->mem_cache.size / 1024, numa_nodes);
		} else {
			pr_dbg("%s: shared cache buffer size: %" PRIu64 "K\n",
				name, g_shared->mem_cache.size / 1024);
		}
	}

	return 0;
}

/*
 *  stress_cache_free()
 *	free shared cache buffer
 */
void stress_cache_free(void)
{
	if (g_shared->mem_cache.buffer)
		(void)stress_munmap_anon_shared((void *)g_shared->mem_cache.buffer, g_shared->mem_cache.size);
	if (g_shared->cacheline.buffer)
		(void)stress_munmap_anon_shared((void *)g_shared->cacheline.buffer, g_shared->cacheline.size);
}
