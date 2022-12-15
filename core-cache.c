/*
 * Copyright (C) 2016-2017 Intel, Ltd.
 * Copyright (C) 2016-2021 Canonical, Ltd.
 * Copyright (C) 2021-2022 Colin Ian King.
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
#include "core-arch.h"
#include "core-cache.h"

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(__linux__) ||	\
    defined(__APPLE__)

typedef struct {
	const char	*name;			/* cache type name */
	const stress_cache_type_t value;	/* cache type ID */
} stress_generic_map_t;

typedef enum {
	CACHE_SIZE,
	CACHE_LINE_SIZE,
	CACHE_WAYS
} cache_size_type_t;

#define SYS_CPU_PREFIX               "/sys/devices/system/cpu"
#define SYS_CPU_CACHE_DIR            "cache"

static const stress_generic_map_t cache_type_map[] = {
	{ "data",		CACHE_TYPE_DATA },
	{ "instruction",	CACHE_TYPE_INSTRUCTION },
	{ "unified",		CACHE_TYPE_UNIFIED },
	{  NULL,		CACHE_TYPE_UNKNOWN }
};

/*
 * stress_cache_get_cpu()
 *
 */
static inline unsigned int stress_cache_get_cpu(const stress_cpus_t *cpus)
{
	const unsigned int cpu = stress_get_cpu();

	return (cpu >= cpus->count) ? 0 : cpu;
}

/*
 * stress_get_string_from_file()
 * 	read data from file into a fixed size buffer
 *	and remove any trailing newlines
 */
static int stress_get_string_from_file(
	const char *path,
	char *tmp,
	const size_t tmp_len)
{
	char *ptr;
	ssize_t ret;

	/* system read will zero fill tmp */
	ret = system_read(path, tmp, tmp_len);
	if (ret < 0)
		return -1;

	ptr = strchr(tmp, '\n');
	if (ptr)
		*ptr = '\0';

	return 0;
}

/*
 * stress_size_to_bytes()
 * 	Convert human-readable integer sizes (such as "32K", "4M") into bytes.
 *
 * Supports:
 *
 * - bytes ('B').
 * - kibibytes ('K' - aka KiB).
 * - mebibytes ('M' - aka MiB).
 * - gibibytes ('G' - aka GiB).
 * - tebibutes ('T' - aka TiB).
 *
 * Returns: size in bytes, or 0 on error.
 */
static uint64_t stress_size_to_bytes(const char *str)
{
	uint64_t bytes;
	int	 ret;
	char	 sz;

	if (!str) {
		pr_dbg("%s: empty string specified\n", __func__);
		return 0;
	}

	ret = sscanf(str, "%" SCNu64 "%c", &bytes, &sz);
	if (ret != 2) {
		pr_dbg("%s: failed to parse suffix from \"%s\"\n",
			__func__, str);
		return 0;
	}

	switch (sz) {
	case 'B':
		/* no-op */
		break;
	case 'K':
		bytes *= KB;
		break;
	case 'M':
		bytes *= MB;
		break;
	case 'G':
		bytes *= GB;
		break;
	case 'T':
		bytes *= TB;
		break;
	default:
		pr_err("unable to convert '%c' size to bytes\n", sz);
		bytes = 0;
		break;
	}
	return bytes;
}

/*
 * stress_get_cache_type()
 * @name: human-readable cache type.
 * Convert a human-readable cache type into a stress_cache_type_t.
 *
 * Returns: stress_cache_type_t or CACHE_TYPE_UNKNOWN on error.
 */
static stress_cache_type_t stress_get_cache_type(const char *name)
{
	const stress_generic_map_t *p;

	if (!name) {
		pr_dbg("%s: no cache type specified\n", __func__);
		goto out;
	}

	for (p = cache_type_map; p && p->name; p++) {
		if (!strcasecmp(p->name, name))
			return p->value;
	}

out:
	return CACHE_TYPE_UNKNOWN;
}

/*
 * stress_add_cpu_cache_detail()
 * @cache: stress_cpu_cache_t pointer.
 * @index_path: full /sys path to the particular cpu cache which is to
 *   be represented by @cache.
 * Populate the specified @cache based on the given cache index.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static int stress_add_cpu_cache_detail(stress_cpu_cache_t *cache, const char *index_path)
{
	int ret = EXIT_FAILURE;
	char tmp[2048];
	char path[PATH_MAX];

	(void)memset(path, 0, sizeof(path));
	if (!cache)
		goto out;
	if (!index_path)
		goto out;
	(void)stress_mk_filename(path, sizeof(path), index_path, "type");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->type = (stress_cache_type_t)stress_get_cache_type(tmp);
	if (cache->type == CACHE_TYPE_UNKNOWN)
		goto out;

	(void)stress_mk_filename(path, sizeof(path), index_path, "size");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->size = stress_size_to_bytes(tmp);

	(void)stress_mk_filename(path, sizeof(path), index_path, "level");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->level = (uint16_t)atoi(tmp);

	(void)stress_mk_filename(path, sizeof(path), index_path, "coherency_line_size");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		goto out;
	cache->line_size = (uint32_t)atoi(tmp);

	(void)stress_mk_filename(path, sizeof(path), index_path, "ways_of_associativity");
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0) {
		cache->ways = 0;
	} else {
		if (sscanf(tmp, "%" SCNu32, &cache->ways) != 1)
			cache->ways = 0;
	}
	ret = EXIT_SUCCESS;
out:
	return ret;
}

/*
 * stress_get_cache_by_cpu()
 * @cpu: cpu to consider.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain the cpu cache indexed by @cache_level.
 *
 * POTENTIAL BUG: assumes only 1 data cache per CPU cache level.
 *
 * Returns: stress_cpu_cache_t, or NULL on error.
 */
static stress_cpu_cache_t * stress_get_cache_by_cpu(const stress_cpu_t *cpu, const int cache_level)
{
	uint32_t  i;

	if (!cpu || !cache_level)
		return NULL;

	for (i = 0; i < cpu->cache_count; i++) {
		stress_cpu_cache_t *p = &cpu->caches[i];

		if (p->level != cache_level)
			continue;

		/* we want a data cache */
		if (p->type != CACHE_TYPE_INSTRUCTION)
			return p;
	}
	return NULL;
}

/*
 * stress_get_max_cache_level()
 * @cpus: array of cpus to query.
 * Determine the maximum cache level available on the system.
 *
 * Returns: 1-index value denoting highest cache level, or 0 on error.
 */
uint16_t stress_get_max_cache_level(const stress_cpus_t *cpus)
{
	stress_cpu_t    *cpu;
	uint32_t  i;
	uint16_t  max = 0;

	if (!cpus) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return 0;
	}

	cpu = &cpus->cpus[stress_cache_get_cpu(cpus)];

	for (i = 0; i < cpu->cache_count; i++) {
		const stress_cpu_cache_t *cache = &cpu->caches[i];

		max = cache->level > max ? cache->level : max;
	}

	return max;
}

/*
 * stress_get_cpu_cache()
 * @cpus: array of cpus to query.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain a cpu cache of level @cache_level.
 *
 * Returns: stress_cpu_cache_t pointer, or NULL on error.
 */
stress_cpu_cache_t *stress_get_cpu_cache(const stress_cpus_t *cpus, const uint16_t cache_level)
{
	stress_cpu_t *cpu;

	if (!cpus) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return NULL;
	}

	if (!cache_level) {
		pr_dbg("%s: invalid cache_level: %d\n",
			__func__, cache_level);
		return NULL;
	}

	cpu = &cpus->cpus[stress_cache_get_cpu(cpus)];

	return stress_get_cache_by_cpu(cpu, cache_level);
}

#if defined(STRESS_ARCH_SPARC)
static int stress_get_cpu_cache_value(
	const char *cpu_path,
	const char *file,
	uint64_t *value)
{
	char path[PATH_MAX];
	char tmp[128];

	(void)stress_mk_filename(path, sizeof(path), cpu_path, file);
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) == 0) {
		if (sscanf(tmp, "%" SCNu64, value) == 1)
			return 0;
	}
	return -1;
}
#endif

/*
 *  stress_get_cpu_cache_auxval()
 *	find cache information as provided by getauxval
 */
static int stress_get_cpu_cache_auxval(stress_cpu_t *cpu)
{
#if defined(HAVE_SYS_AUXV_H) && 	\
    defined(HAVE_GETAUXVAL) &&		\
    (defined(AT_L1D_CACHESIZE) ||	\
     defined(AT_L1I_CACHESIZE) ||	\
     defined(AT_L2_CACHESIZE) ||	\
     defined(AT_L3_CACHESIZE))
	typedef struct {
		const unsigned long auxval_type;
		const stress_cache_type_t type;		/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t index;			/* map to cpu->cache array index */
	} cache_auxval_info_t;

	static const cache_auxval_info_t cache_auxval_info[] = {
#if defined(AT_L1D_CACHESIZE)
		{ AT_L1D_CACHESIZE,	CACHE_TYPE_DATA,	1, CACHE_SIZE,	0 },
#endif
#if defined(AT_L1I_CACHESIZE)
		{ AT_L1I_CACHESIZE,	CACHE_TYPE_INSTRUCTION,	1, CACHE_SIZE,	1 },
#endif
#if defined(AT_L2_CACHESIZE)
		{ AT_L2_CACHESIZE,	CACHE_TYPE_UNIFIED,	2, CACHE_SIZE,	2 },
#endif
#if defined(AT_L3_CACHESIZE)
		{ AT_L3_CACHESIZE,	CACHE_TYPE_UNIFIED,	3, CACHE_SIZE,	2 },
#endif
	};

	const size_t count = 4;
	size_t i;
	bool valid = false;

	cpu->caches = calloc(count, sizeof(*(cpu->caches)));
	if (!cpu->caches) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_auxval_info); i++) {
		const uint64_t value = getauxval(cache_auxval_info[i].auxval_type);
		const size_t index = cache_auxval_info[i].index;

		if (value)
			valid = true;

		cpu->caches[index].type = cache_auxval_info[i].type;
		cpu->caches[index].level = cache_auxval_info[i].level;
		switch (cache_auxval_info[i].size_type) {
		case CACHE_SIZE:
			cpu->caches[index].size = value;
			break;
		case CACHE_LINE_SIZE:
			cpu->caches[index].line_size = (uint32_t)value;
			break;
		case CACHE_WAYS:
			cpu->caches[index].size = (uint32_t)value;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}

	cpu->cache_count = count;

	return count;
#else
	(void)cpu;

	return 0;
#endif
}

#if defined(STRESS_ARCH_ALPHA)
/*
 *  stress_get_cpu_cache_alpha()
 *	find cache information as provided by linux Alpha from
 *	/proc/cpu. Assume cache layout for 1st CPU is same for
 *	all CPUs.
 */
static int stress_get_cpu_cache_alpha(
	stress_cpu_t *cpu,
	const char *cpu_path)
{
	FILE *fp;
	const size_t count = 4;
	size_t idx = 0;

	(void)cpu_path;

	/*
	 * parse /proc/cpu info in the form:
	 * L1 Icache		: 64K, 2-way, 64b line
	 * L1 Dcache		: 64K, 2-way, 64b line
	 * L2 cache		: n/a
	 * L3 cache		: n/a
	 */
	cpu->caches = calloc(count, sizeof(*(cpu->caches)));
	if (!cpu->caches) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	fp = fopen("/proc/cpuinfo", "r");
	if (fp) {
		char buffer[4096];

		while ((idx < count) && fgets(buffer, sizeof(buffer), fp)) {
			stress_cache_type_t cache_type = CACHE_TYPE_UNKNOWN;
			uint16_t cache_level = 0;
			char *ptr;
			int cache_size, cache_ways, cache_line_size, n;

			if (!strncmp("L1 Icache", buffer, 9)) {
				cache_type = CACHE_TYPE_INSTRUCTION;
				cache_level = 1;
			} else if (!strncmp("L1 Dcache", buffer, 9))  {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 1;
			} else if (!strncmp("L2 cache", buffer, 8)) {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 2;
			} else if (!strncmp("L3 cache", buffer, 8)) {
				cache_type = CACHE_TYPE_DATA;
				cache_level = 3;
			} else {
				continue;
			}
			ptr = strchr(buffer, ':');
			if (!ptr)
				continue;
			ptr++;
			cache_size = 0;
			cache_ways = 0;
			cache_line_size = 0;
			n = sscanf(ptr, "%dK, %d-way, %db line",
				&cache_size, &cache_ways, &cache_line_size);
			if (n != 3)
				continue;
			cpu->caches[idx].type = cache_type;
			cpu->caches[idx].level = cache_level;
			cpu->caches[idx].size = cache_size * 1024;
			cpu->caches[idx].ways = cache_ways;
			cpu->caches[idx].line_size = cache_line_size;
			idx++;
		}
		(void)fclose(fp);
	}

	if (idx == 0) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = idx;

	return idx;
}
#endif

#if defined(__APPLE__)
/*
 *  stress_get_cpu_cache_apple()
 *	find cache information as provided by BSD sysctl
 */
static int stress_get_cpu_cache_apple(stress_cpu_t *cpu)
{
	typedef struct {
		const char *name;			/* sysctl name */
		const stress_cache_type_t type;		/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t index;			/* map to cpu->cache array index */
	} cache_info_t;

	static const cache_info_t cache_info[] = {
		{ "hw.cachelinesize",		CACHE_TYPE_DATA,	1, CACHE_LINE_SIZE,	0 },
		{ "hw.l1dcachesize",		CACHE_TYPE_DATA,	1, CACHE_SIZE,		0 },
		{ "hw.cachelinesize",		CACHE_TYPE_INSTRUCTION,	1, CACHE_LINE_SIZE,	1 },
		{ "hw.l1icachesize",		CACHE_TYPE_INSTRUCTION,	1, CACHE_SIZE,		1 },
		{ "hw.l2cachesize",		CACHE_TYPE_UNIFIED,	2, CACHE_SIZE,		2 },
		{ "hw.l3cachesize",		CACHE_TYPE_UNIFIED,	3, CACHE_SIZE,		2 },
	};

	const size_t count = 3;
	size_t i;
	bool valid = false;

	cpu->caches = calloc(count, sizeof(*(cpu->caches)));
	if (!cpu->caches) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_info); i++) {
		const size_t idx = cache_info[i].index;
		uint64_t value;

		value = stress_bsd_getsysctl_uint64(cache_info[i].name);

		cpu->caches[idx].type = cache_info[i].type;
		cpu->caches[idx].level = cache_info[i].level;
		switch (cache_info[i].size_type) {
		case CACHE_SIZE:
			cpu->caches[idx].size = value;
			valid = true;
			break;
		case CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			valid = true;
			break;
		case CACHE_WAYS:
			cpu->caches[idx].size = (uint32_t)value;
			valid = true;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = count;

	return count;
}
#endif

#if defined(STRESS_ARCH_SPARC)
/*
 *  stress_get_cpu_cache_sparc64()
 *	find cache information as provided by linux SPARC64
 *	/sys/devices/system/cpu/cpu0
 */
static int stress_get_cpu_cache_sparc64(
	stress_cpu_t *cpu,
	const char *cpu_path)
{
	typedef struct {
		const char *filename;			/* /sys proc name */
		const stress_cache_type_t type;		/* cache type */
		const uint16_t level;			/* cache level 1, 2 */
		const cache_size_type_t size_type;	/* cache size field */
		const size_t index;			/* map to cpu->cache array index */
	} cache_info_t;

	static const cache_info_t cache_info[] = {
		{ "l1_dcache_line_size",	CACHE_TYPE_DATA,	1, CACHE_LINE_SIZE,	0 },
		{ "l1_dcache_size",		CACHE_TYPE_DATA,	1, CACHE_SIZE,		0 },
		{ "l1_icache_line_size",	CACHE_TYPE_INSTRUCTION,	1, CACHE_LINE_SIZE,	1 },
		{ "l1_icache_size",		CACHE_TYPE_INSTRUCTION,	1, CACHE_SIZE,		1 },
		{ "l2_cache_line_size",		CACHE_TYPE_UNIFIED,	2, CACHE_LINE_SIZE,	2 },
		{ "l2_cache_size",		CACHE_TYPE_UNIFIED,	2, CACHE_SIZE,		2 },
	};

	const size_t count = 3;
	size_t i;
	bool valid = false;

	cpu->caches = calloc(count, sizeof(*(cpu->caches)));
	if (!cpu->caches) {
		pr_err("failed to allocate %zu bytes for cpu caches\n",
			count * sizeof(*(cpu->caches)));
		return 0;
	}

	for (i = 0; i < SIZEOF_ARRAY(cache_info); i++) {
		const size_t idx = cache_info[i].index;
		uint64_t value;

		if (stress_get_cpu_cache_value(cpu_path, cache_info[i].filename, &value) < 0)
			continue;

		cpu->caches[idx].type = cache_info[i].type;
		cpu->caches[idx].level = cache_info[i].level;
		switch (cache_info[i].size_type) {
		case CACHE_SIZE:
			cpu->caches[idx].size = value;
			valid = true;
			break;
		case CACHE_LINE_SIZE:
			cpu->caches[idx].line_size = (uint32_t)value;
			valid = true;
			break;
		case CACHE_WAYS:
			cpu->caches[idx].size = (uint32_t)value;
			valid = true;
			break;
		default:
			break;
		}
	}

	if (!valid) {
		free(cpu->caches);
		cpu->caches = NULL;
		cpu->cache_count = 0;

		return 0;
	}
	cpu->cache_count = count;

	return count;
}
#endif

/*
 *  index_filter()
 *	return 1 when filename is index followed by a digit
 */
static int index_filter(const struct dirent *d)
{
	return ((strncmp(d->d_name, "index", 5) == 0) && isdigit(d->d_name[5]));
}

/*
 *  index_sort()
 *	sort by index number (digits 5 onwards)
 */
static int index_sort(const struct dirent **d1, const struct dirent **d2)
{
	const int i1 = atoi(&(*d1)->d_name[5]);
	const int i2 = atoi(&(*d2)->d_name[5]);

	return i1 - i2;
}

/*
 *  cpu_filter()
 *	return 1 when filename is cpu followed by a digit
 */
static int cpu_filter(const struct dirent *d)
{
	return ((strncmp(d->d_name, "cpu", 3) == 0) && isdigit(d->d_name[3]));
}

/*
 *  cpu_sort()
 *	sort by CPU number (digits 3 onwards)
 */
static int cpusort(const struct dirent **d1, const struct dirent **d2)
{
	const int c1 = atoi(&(*d1)->d_name[3]);
	const int c2 = atoi(&(*d2)->d_name[3]);

	return c1 - c2;
}

/*
 *  stress_get_cpu_cache_index()
 *	find cache information as provided by cache info indexes
 *	in /sys/devices/system/cpu/cpu*
 */
static int stress_get_cpu_cache_index(
	stress_cpu_t *cpu,
	const char *cpu_path)
{
	struct dirent **namelist = NULL;
	int n;
	uint32_t i;
	char path[PATH_MAX];

	(void)stress_mk_filename(path, sizeof(path), cpu_path, SYS_CPU_CACHE_DIR);
	n = scandir(path, &namelist, index_filter, index_sort);
	if (n <= 0) {
		cpu->caches = NULL;
		return 0;
	}
	cpu->cache_count = (uint32_t)n;
	cpu->caches = calloc(cpu->cache_count, sizeof(*(cpu->caches)));
	if (!cpu->caches) {
		size_t cache_bytes = cpu->cache_count * sizeof(*(cpu->caches));

		pr_err("failed to allocate %zu bytes for cpu caches\n",
			cache_bytes);

		cpu->caches = NULL;
		cpu->cache_count = 0;
		goto list_free;
	}

	for (i = 0; i < cpu->cache_count; i++) {
		const char *name = namelist[i]->d_name;
		char fullpath[PATH_MAX];

		(void)memset(fullpath, 0, sizeof(fullpath));
		(void)stress_mk_filename(fullpath, sizeof(fullpath), path, name);
		if (stress_add_cpu_cache_detail(&cpu->caches[i], fullpath) != EXIT_SUCCESS) {
			free(cpu->caches);
			cpu->caches = NULL;
			cpu->cache_count = 0;

			goto list_free;
		}
	}
list_free:
	n = (int)cpu->cache_count;
	stress_dirent_list_free(namelist, n);

	return n;
}

/*
 * stress_get_cpu_cache_details()
 * @cpu: cpu to fill in.
 * @cpu_path: Full /sys path to cpu which will be represented by @cpu.
 * Populate @cpu with details from @cpu_path.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static void stress_get_cpu_cache_details(stress_cpu_t *cpu, const char *cpu_path)
{
	if (!cpu) {
		pr_dbg("%s: invalid cpu parameter\n", __func__);
		return;
	}
	if (!cpu_path) {
		pr_dbg("%s: invalid cpu path parameter\n", __func__);
		return;
	}

	/* The default x86 cache method */
	if (stress_get_cpu_cache_index(cpu, cpu_path) > 0)
		return;

	/* Try cache info using auxinfo */
	if (stress_get_cpu_cache_auxval(cpu) > 0)
		return;

#if defined(STRESS_ARCH_SPARC)
	/* Try cache info for sparc CPUs */
	if (stress_get_cpu_cache_sparc64(cpu, cpu_path) > 0)
		return;
#endif

#if defined(STRESS_ARCH_ALPHA)
	if (stress_get_cpu_cache_alpha(cpu, cpu_path) > 0)
		return;
#endif

#if defined(__APPLE__)
	if (stress_get_cpu_cache_apple(cpu) > 0)
		return;
#endif

	return;
}

#if defined(__linux__)
/*
 * stress_get_all_cpu_cache_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated stress_cpus_t object, or NULL on error.
 */
stress_cpus_t *stress_get_all_cpu_cache_details(void)
{
	int i, cpu_count;
	stress_cpus_t *cpus = NULL;
	struct dirent **namelist = NULL;

	cpu_count = scandir(SYS_CPU_PREFIX, &namelist, cpu_filter, cpusort);
	if (cpu_count < 1) {
		pr_err("no CPUs found in %s\n", SYS_CPU_PREFIX);
		goto out;
	}
	cpus = calloc(1, sizeof(*cpus));
	if (!cpus)
		goto out;

	cpus->cpus = calloc((size_t)cpu_count, sizeof(*(cpus->cpus)));
	if (!cpus->cpus) {
		free(cpus);
		cpus = NULL;
		goto out;
	}
	cpus->count = (uint32_t)cpu_count;

	for (i = 0; i < cpu_count; i++) {
		const char *name = namelist[i]->d_name;
		char fullpath[PATH_MAX];
		stress_cpu_t *const cpu = &cpus->cpus[i];

		(void)memset(fullpath, 0, sizeof(fullpath));
		(void)stress_mk_filename(fullpath, sizeof(fullpath), SYS_CPU_PREFIX, name);
		cpu->num = (uint32_t)i;
		if (cpu->num == 0) {
			/* 1st CPU cannot be taken offline */
			cpu->online = 1;
		} else {
			char onlinepath[PATH_MAX + 8];
			char tmp[2048];

			(void)memset(onlinepath, 0, sizeof(onlinepath));
			(void)snprintf(onlinepath, sizeof(onlinepath), "%s/%s/online", SYS_CPU_PREFIX, name);
			if (stress_get_string_from_file(onlinepath, tmp, sizeof(tmp)) < 0) {
				/* Assume it is online, it is the best we can do */
				cpu->online = 1;
			} else {
				cpu->online = atoi(tmp);
			}
		}
		if (cpu->online)
			stress_get_cpu_cache_details(&cpus->cpus[i], fullpath);
	}

out:
	stress_dirent_list_free(namelist, cpu_count);
	return cpus;
}
#endif

#if defined(__APPLE__)
/*
 * stress_get_all_cpu_cache_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated stress_cpus_t object, or NULL on error.
 */
stress_cpus_t *stress_get_all_cpu_cache_details(void)
{
	int32_t i, cpu_count;
	stress_cpus_t *cpus = NULL;
	struct dirent **namelist = NULL;

	if (stress_bsd_getsysctl("hw.physicalcpu", &cpu_count, sizeof(cpu_count)) < 0) {
		pr_err("no CPUs found using sysctl hw.physicalcpu\n");
		goto out;
	}
	cpus = calloc(1, sizeof(*cpus));
	if (!cpus)
		goto out;

	cpus->cpus = calloc((size_t)cpu_count, sizeof(*(cpus->cpus)));
	if (!cpus->cpus) {
		free(cpus);
		cpus = NULL;
		goto out;
	}
	cpus->count = (uint32_t)cpu_count;

	for (i = 0; i < cpu_count; i++) {
		stress_get_cpu_cache_details(&cpus->cpus[i], "");
	}

out:
	stress_dirent_list_free(namelist, cpu_count);
	return cpus;
}
#endif

/*
 * stress_free_cpu_caches()
 * @cpus: value returned by get_all_cpu_cache_details().
 *
 * Undo the action of get_all_cpu_cache_details() by freeing all
 * associated resources.
 */
void stress_free_cpu_caches(stress_cpus_t *cpus)
{
	uint32_t  i;

	if (!cpus)
		return;

	for (i = 0; i < cpus->count; i++) {
		stress_cpu_t *cpu = &cpus->cpus[i];

		if (cpu->caches) {
			free(cpu->caches);
			cpu->caches = NULL;
		}
	}
	free(cpus->cpus);
	cpus->cpus = NULL;
	free(cpus);
}

#endif

/*
 *  stress_get_llc_size()
 * 	get Lower Level Cache size and Cache Line size (sizes in bytes)
 *	sizes are zero if not available.
 */
void stress_get_llc_size(size_t *llc_size, size_t *cache_line_size)
{
#if defined(__linux__) ||	\
    defined(__APPLE__)
	uint16_t max_cache_level;
	stress_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;

	*llc_size = 0;
	*cache_line_size = 0;

	cpu_caches = stress_get_all_cpu_cache_details();
	if (!cpu_caches)
		return;

	max_cache_level = stress_get_max_cache_level(cpu_caches);
	if (max_cache_level < 1)
		goto free_cpu_caches;
	cache = stress_get_cpu_cache(cpu_caches, max_cache_level);
	if (!cache)
		goto free_cpu_caches;

	*llc_size = cache->size;
	*cache_line_size = cache->line_size ? cache->line_size : 64;

free_cpu_caches:
	stress_free_cpu_caches(cpu_caches);
#else
	*llc_size = 0;
	*cache_line_size = 0;
#endif
}
