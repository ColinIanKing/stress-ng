/*
 * Copyright (C) 2016-2017 Intel, Ltd.
 * Copyright (C) 2016-2021 Canonical, Ltd.
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

#if defined(__linux__)

typedef struct {
	const char	*name;		/* cache type name */
	const uint32_t	value;		/* cache type ID */
} stress_generic_map_t;

#if defined(__linux__)
#define SYS_CPU_PREFIX               "/sys/devices/system/cpu"
#define SYS_CPU_CACHE_DIR            "cache"
#endif

static const stress_generic_map_t cache_type_map[] = {
	{"data"        , CACHE_TYPE_DATA},
	{"instruction" , CACHE_TYPE_INSTRUCTION},
	{"unified"     , CACHE_TYPE_UNIFIED},
	{ NULL         , CACHE_TYPE_UNKNOWN}
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
	int ret;

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
 *
 * Returns: size in bytes, or 0 on error.
 */
static uint64_t stress_size_to_bytes(const char *str)
{
	uint64_t            bytes;
	uint64_t            multiplier;
	unsigned long int   value;
	int                 ret;
	char               *s;

	if (!str) {
		pr_dbg("%s: empty string specified\n", __func__);
		return 0;
	}

	ret = sscanf(str, "%lu%ms", &value, &s);
	if ((ret != 2) || !s) {
		pr_dbg("%s: failed to parse suffix from \"%s\"\n",
			__func__, str);
		return 0;
	}

	switch (*s) {
	case 'B':
		multiplier = 1;
		break;
	case 'K':
		multiplier = KB;
		break;
	case 'M':
		multiplier = MB;
		break;
	case 'G':
		multiplier = GB;
		break;
	default:
		pr_err("unable to convert string to bytes: %s\n", str);
		bytes = 0;
		goto out;
	}

	bytes = value * multiplier;

out:
	free(s);
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
	const size_t index_posn = index_path ? strlen(index_path) : 0;
	const size_t path_len = index_posn + 32;
	int ret = EXIT_FAILURE;
	char tmp[2048];
	char path[path_len];

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
	if (stress_get_string_from_file(path, tmp, sizeof(tmp)) < 0)
		cache->ways = 0;
	else
		cache->ways = atoi(tmp);
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
		stress_cpu_cache_t *p;

		p = &cpu->caches[i];
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
		stress_cpu_cache_t *cache;

		cache = &cpu->caches[i];
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
stress_cpu_cache_t * stress_get_cpu_cache(const stress_cpus_t *cpus, const uint16_t cache_level)
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

/*
 * stress_get_cpu_cache_details()
 * @cpu: cpu to fill in.
 * @cpu_path: Full /sys path to cpu which will be represented by @cpu.
 * Populate @cpu with details from @cpu_path.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static int stress_get_cpu_cache_details(stress_cpu_t *cpu, const char *cpu_path)
{
	const size_t cpu_path_len = cpu_path ? strlen(cpu_path) : 0;
	char path[cpu_path_len + strlen(SYS_CPU_CACHE_DIR) + 2];
	int i, j, n, ret = EXIT_FAILURE;
	struct dirent **namelist = NULL;

	if (!cpu) {
		pr_dbg("%s: invalid cpu parameter\n", __func__);
		return ret;
	}
	if (!cpu_path) {
		pr_dbg("%s: invalid cpu path parameter\n", __func__);
		return ret;
	}

	(void)stress_mk_filename(path, sizeof(path), cpu_path, SYS_CPU_CACHE_DIR);
	n = scandir(path, &namelist, NULL, alphasort);
	if (n < 0) {
		/*
		 * Not an error since some platforms don't provide cache
		 * details * via /sys (ARM).
		 */
		return ret;
	}

	cpu->cache_count = 0;
	for (i = 0; i < n; i++) {
		if (!strncmp(namelist[i]->d_name, "index", 5))
			cpu->cache_count++;
	}
	if (!cpu->cache_count) {
		if (stress_warn_once())
			pr_err("no CPU caches found\n");
		goto err;
	}

	cpu->caches = calloc(cpu->cache_count, sizeof(stress_cpu_cache_t));
	if (!cpu->caches) {
		size_t cache_bytes = cpu->cache_count * sizeof(stress_cpu_cache_t);

		pr_err("failed to allocate %zu bytes for cpu caches\n",
			cache_bytes);
		goto err;
	}

	for (i = 0, j = 0; i < n; i++) {
		const char *name = namelist[i]->d_name;

		if (!strncmp(name, "index", 5) &&
		    isdigit(name[5])) {
			char fullpath[strlen(path) + strlen(name) + 2];

			(void)memset(fullpath, 0, sizeof(fullpath));
			(void)stress_mk_filename(fullpath, sizeof(fullpath), path, name);
			if (stress_add_cpu_cache_detail(&cpu->caches[j++], fullpath) != EXIT_SUCCESS)
				goto err;
		}
	}
	ret = EXIT_SUCCESS;
err:
	stress_dirent_list_free(namelist, n);

	return ret;
}

/*
 * stress_get_all_cpu_cache_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated stress_cpus_t object, or NULL on error.
 */
stress_cpus_t *stress_get_all_cpu_cache_details(void)
{
	int i, j, n, ret, cpu_count;
	stress_cpus_t *cpus = NULL;
	struct dirent **namelist = NULL;

	n = scandir(SYS_CPU_PREFIX, &namelist, NULL, alphasort);
	if (n < 0) {
		pr_err("no CPUs found - is /sys mounted?\n");
		return 0;
	}
	for (cpu_count = 0, i = 0; i < n; i++) {
		const char *name = namelist[i]->d_name;

		if (!strncmp(name, "cpu", 3) && isdigit(name[3]))
			cpu_count++;
	}
	if (cpu_count < 1) {
		/* Maybe we should check this? */
		pr_err("no CPUs found in %s\n", SYS_CPU_PREFIX);
		goto out;
	}
	cpus = calloc(1, sizeof(stress_cpus_t));
	if (!cpus)
		goto out;

	cpus->cpus = calloc(cpu_count, sizeof(stress_cpu_t));
	if (!cpus->cpus) {
		free(cpus);
		cpus = NULL;
		goto out;
	}
	cpus->count = cpu_count;

	for (i = 0, j = 0; (i < n) && (j < cpu_count); i++) {
		const char *name = namelist[i]->d_name;
		const size_t fullpath_len = strlen(SYS_CPU_PREFIX) + strlen(name) + 2;

		if (!strncmp(name, "cpu", 3) && isdigit(name[3])) {
			char fullpath[fullpath_len];
			stress_cpu_t *const cpu = &cpus->cpus[j];

			(void)memset(fullpath, 0, sizeof(fullpath));
			(void)stress_mk_filename(fullpath, sizeof(fullpath), SYS_CPU_PREFIX, name);
			cpu->num = j;
			if (j == 0) {
				/* 1st CPU cannot be taken offline */
				cpu->online = 1;
			} else {
				char onlinepath[fullpath_len + 8];
				char tmp[2048];

				(void)memset(onlinepath, 0, sizeof(onlinepath));
				(void)snprintf(onlinepath, sizeof(onlinepath), "%s/%s/online", SYS_CPU_PREFIX, name);
				if (stress_get_string_from_file(onlinepath, tmp, sizeof(tmp)) < 0) {
					/* Assume it is online, it is the best we can do */
					cpu->online = 1;
				} else  {
					cpu->online = atoi(tmp);
				}
			}

			ret = stress_get_cpu_cache_details(&cpus->cpus[j], fullpath);
			if (ret != EXIT_SUCCESS) {
				free(cpus->cpus);
				free(cpus);
				cpus = NULL;
				goto out;
			}
			j++;
		}
	}

out:
	stress_dirent_list_free(namelist, n);
	return cpus;
}

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
