/*
 * Copyright (C) 2016-2017 Intel, Ltd.
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
} generic_map_t;

#include <glob.h>

#if defined(__linux__)
#define SYS_CPU_PREFIX               "/sys/devices/system/cpu"
#define GLOB_PATTERN SYS_CPU_PREFIX  "/cpu[0-9]*"
#define SYS_CPU_CACHE_DIR            "/cache"
#define GLOB_PATTERN_INDEX_PREFIX    "/index[0-9]*"
#endif

/*
 * append @element to array @path (which has len @len)
 */
#define MK_PATH(path, len, element) 			\
	memset((path) + len, '\0', sizeof(path) - len);	\
	strncpy((path) + len, element, strlen(element))

static const generic_map_t cache_type_map[] = {
	{"data"        , CACHE_TYPE_DATA},
	{"instruction" , CACHE_TYPE_INSTRUCTION},
	{"unified"     , CACHE_TYPE_UNIFIED},
	{ NULL         , CACHE_TYPE_UNKNOWN}
};

static const char *get_cache_name(cache_type_t type)
	__attribute__((unused));

/**
 *
 * cache_get_cpu()
 *
 **/
static inline unsigned int cache_get_cpu(const cpus_t *cpus)
{
	const unsigned int cpu = stress_get_cpu();

	return (cpu >= cpus->count) ? 0 : cpu;
}

/**
 * file_exists()
 * @path: file to check.
 * Determine if specified file exists.
 *
 * Returns: file type if @path exists, else 0.
 **/
static int file_exists(const char *path)
{
	struct stat st;

	if (!path) {
		pr_dbg("%s: empty path specified\n", __func__);
		return 0;
	}

	if (stat(path, &st) < 0)
		return 0;

	return (st.st_mode & S_IFMT);
}

/*
 * get_contents()
 * @path: file to read.
 * Reads the contents of @file, returning the value as a string.
 *
 * Returns: dynamically-allocated copy of the contents of @path,
 * or NULL on error.
 */
static char *get_contents(const char *path)
{
	FILE         *fp = NULL;
	char         *contents = NULL;
	struct stat   st;
	size_t        size;

	if (!path) {
		pr_dbg("%s: empty path specified\n", __func__);
		return NULL;
	}

	fp = fopen(path, "r");
	if (!fp)
		return NULL;

	if (fstat(fileno(fp), &st) < 0)
		goto err_close;

	size = st.st_size;

	contents = malloc(size);
	if (!contents)
		goto err_close;

	if (!fgets(contents, size, fp))
		goto err;

	(void)fclose(fp);
	return contents;

err:
	free(contents);
err_close:
	(void)fclose(fp);
	return NULL;
}

/*
 * get_string_from_file()
 * @path: file to read contents of.
 *
 * Returns: dynamically-allocated copy of the contents of @path,
 * or NULL on error.
 */
static char *get_string_from_file(const char *path)
{
	char   *str;
	ssize_t  len;

	str = get_contents(path);
	if (!str)
		return NULL;

	len = strlen(str) - 1;
	if ((len >= 0) && (str[len] == '\n'))
		str[len] = '\0';

	return str;
}

/*
 * size_to_bytes()
 * Convert human-readable integer sizes (such as "32K", "4M") into bytes.
 *
 * Supports:
 *
 * - bytes ('B').
 * - kibibytes ('K' - aka KiB).
 * - mebibytes ('M' - aka MiB).
 *
 * Returns: size in bytes, or 0 on error.
 */
static uint64_t size_to_bytes(const char *str)
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
	if (ret != 2 || !s) {
		pr_dbg("%s: failed to parse suffix from \"%s\"\n",
			__func__, str);
		return 0;
	}

	switch(*s) {
	case 'B':
		multiplier = 1;
		break;
	case 'K':
		multiplier = 1024;
		break;
	case 'M':
		multiplier = (1024 * 1024);
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

/* get_cache_type()
 * @name: human-readable cache type.
 * Convert a human-readable cache type into a cache_type_t.
 *
 * Returns: cache_type_t or CACHE_TYPE_UNKNOWN on error.
 */
static cache_type_t get_cache_type(const char *name)
{
	const generic_map_t *p;

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
 * get_cache_name()
 * @type: cache_type_t to convert into a string.
 * Convert a cache_type_t to a human-readable cache type.
 *
 * Returns: read-only string representation, or NULL on error.
 */
static const char * get_cache_name(const cache_type_t type)
{
	const generic_map_t *p;

	for (p = cache_type_map; p && p->name; p++) {
		if (p->value == type)
			return p->name;
	}

	return NULL;
}

/* add_cpu_cache_detail()
 * @cache: cpu_cache_t pointer.
 * @index_path: full /sys path to the particular cpu cache which is to
 *   be represented by @cache.
 * Populate the specified @cache based on the given cache index.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static int add_cpu_cache_detail(cpu_cache_t *cache, const char *index_path)
{
	char     path[PATH_MAX] = { 0 };
	size_t   len;
	char    *contents = NULL;
	int      ret = EXIT_FAILURE;

	if (!cache) {
		pr_dbg("%s: invalid cache specified\n", __func__);
		goto out;
	}

	if (!index_path) {
		pr_dbg("%s: invalid index specified\n", __func__);
		goto out;
	}

	len = strlen(index_path);
	strncpy(path, index_path, len);

	MK_PATH(path, len, "/type");
	contents = get_string_from_file(path);
	if (!contents)
		goto out;

	cache->type = (cache_type_t)get_cache_type(contents);
	if (cache->type == CACHE_TYPE_UNKNOWN)
		goto out;
	free(contents);

	MK_PATH(path, len, "/size");
	contents = get_string_from_file(path);
	if (!contents)
		goto out;

	cache->size = size_to_bytes(contents);
	free(contents);

	MK_PATH(path, len, "/level");
	contents = get_string_from_file(path);
	if (!contents)
		goto out;

	cache->level = (uint16_t)atoi(contents);
	free(contents);

	MK_PATH(path, len, "/coherency_line_size");
	contents = get_string_from_file(path);
	if (!contents)
		goto out;

	cache->line_size = (uint32_t)atoi(contents);
	free(contents);

	MK_PATH(path, len, "/ways_of_associativity");
	contents = get_string_from_file(path);

	/* Don't error if file is not readable: cache may not be
	 * way-based.
	 */
	cache->ways = contents ? atoi(contents) : 0;

	ret = EXIT_SUCCESS;

out:
	if (contents)
		free(contents);

	return ret;
}

/*
 * get_cache_by_cpu()
 * @cpu: cpu to consider.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain the cpu cache indexed by @cache_level.
 *
 * POTENTIAL BUG: assumes only 1 data cache per CPU cache level.
 *
 * Returns: cpu_cache_t, or NULL on error.
 */
static cpu_cache_t * get_cache_by_cpu(const cpu_t *cpu, const int cache_level)
{
	uint32_t  i;

	if (!cpu || !cache_level)
		return NULL;

	for (i = 0; i < cpu->cache_count; i++) {
		cpu_cache_t *p;

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
 * get_max_cache_level()
 * @cpus: array of cpus to query.
 * Determine the maximum cache level available on the system.
 *
 * Returns: 1-index value denoting highest cache level, or 0 on error.
 */
uint16_t get_max_cache_level(const cpus_t *cpus)
{
	cpu_t    *cpu;
	uint32_t  i;
	uint16_t  max = 0;

	if (!cpus) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return 0;
	}

	cpu = &cpus->cpus[cache_get_cpu(cpus)];

	for (i = 0; i < cpu->cache_count; i++) {
		cpu_cache_t *cache;

		cache = &cpu->caches[i];
		max = cache->level > max ? cache->level : max;
	}

	return max;
}

/*
 * get_cpu_cache()
 * @cpus: array of cpus to query.
 * @cache_level: numeric cache level (1-indexed).
 * Obtain a cpu cache of level @cache_level.
 *
 * Returns: cpu_cache_t pointer, or NULL on error.
 */
cpu_cache_t * get_cpu_cache(const cpus_t *cpus, const uint16_t cache_level)
{
	cpu_t *cpu;

	if (!cpus) {
		pr_dbg("%s: invalid cpus parameter\n", __func__);
		return NULL;
	}

	if (!cache_level) {
		pr_dbg("%s: invalid cache_level: %d\n",
			__func__, cache_level);
		return NULL;
	}

	cpu = &cpus->cpus[cache_get_cpu(cpus)];

	return get_cache_by_cpu(cpu, cache_level);
}

/*
 * get_cpu_cache_details()
 * @cpu: cpu to fill in.
 * @cpu_path: Full /sys path to cpu which will be represented by @cpu.
 * Populate @cpu with details from @cpu_path.
 *
 * Returns: EXIT_FAILURE or EXIT_SUCCESS.
 */
static int get_cpu_cache_details(cpu_t *cpu, const char *cpu_path)
{
	uint32_t   i;
	size_t     len, len2;
	glob_t     globbuf;
	char       glob_path[PATH_MAX] = { 0 };
	char     **results;
	int        ret = EXIT_FAILURE;
	int        ret2;

	memset(&globbuf, 0, sizeof(globbuf));
	if (!cpu) {
		pr_dbg("%s: invalid cpu parameter\n", __func__);
		return ret;
	}

	if (!cpu_path) {
		pr_dbg("%s: invalid cpu path parameter\n", __func__);
		return ret;
	}

	len = strlen(cpu_path);
	strncat(glob_path, cpu_path, len);

	len2 = strlen(SYS_CPU_CACHE_DIR);
	strncat(glob_path, SYS_CPU_CACHE_DIR, len2);
	len += len2;

	ret2 = file_exists(glob_path);
	if (!ret2) {
		/*
		 * Not an error since some platforms don't provide cache
		 * details * via /sys (ARM).
		 */
		if (warn_once(WARN_ONCE_NO_CACHE))
			pr_dbg("%s does not exist\n", glob_path);
		return ret;
	}

	if (ret2 != S_IFDIR) {
		if (warn_once(WARN_ONCE_NO_CACHE))
			pr_err("file %s is not a directory\n",
				glob_path);
		return ret;
	}

	strncat(glob_path, GLOB_PATTERN_INDEX_PREFIX,
		sizeof(glob_path) - len - 1);
	ret2 = glob(glob_path, GLOB_ONLYDIR, NULL, &globbuf);

	if (ret2 != 0) {
		if (warn_once(WARN_ONCE_NO_CACHE))
			pr_err("glob on regex \"%s\" failed: %d\n",
				glob_path, ret);
		return ret;
	}

	results = globbuf.gl_pathv;
	cpu->cache_count = globbuf.gl_pathc;

	if (!cpu->cache_count) {
		if (warn_once(WARN_ONCE_NO_CACHE))
			pr_err("no CPU caches found\n");
		goto err;
	}

	cpu->caches = calloc(cpu->cache_count, sizeof(cpu_cache_t));
	if (!cpu->caches) {
		size_t cache_bytes = cpu->cache_count * sizeof(cpu_cache_t);

		pr_err("failed to allocate %zu bytes for cpu caches\n",
			cache_bytes);
		goto err;
	}

	for (i = 0; i < cpu->cache_count; i++) {
		ret2 = add_cpu_cache_detail(&cpu->caches[i], results[i]);
		if (ret2 != EXIT_SUCCESS)
			goto err;
	}

	ret = EXIT_SUCCESS;

err:
	globfree(&globbuf);

	/* reset */
	glob_path[0] = '\0';

	return ret;
}

/*
 * get_all_cpu_cache_details()
 * Obtain information on all cpus caches on the system.
 *
 * Returns: dynamically-allocated cpus_t object, or NULL on error.
 */
cpus_t * get_all_cpu_cache_details(void)
{
	uint32_t   i;
	int        ret;
	char       path[PATH_MAX] = { 0 };
	glob_t     globbuf;
	char     **results;
	cpus_t    *cpus = NULL;
	size_t     cpu_count;
	size_t     len;

	memset(&globbuf, 0, sizeof(globbuf));

	ret = file_exists(SYS_CPU_PREFIX);
	if (!ret) {
		pr_err("%s does not exist\n", SYS_CPU_PREFIX);
		return NULL;
	}

	if (ret != S_IFDIR) {
		pr_err("file %s is not a directory\n", SYS_CPU_PREFIX);
		return NULL;
	}

	ret = glob(GLOB_PATTERN, GLOB_ONLYDIR, NULL, &globbuf);

	if (ret != 0) {
		pr_err("glob on regex \"%s\" failed: %d\n",
			GLOB_PATTERN, ret);
		return NULL;
	}

	results = globbuf.gl_pathv;
	cpu_count = globbuf.gl_pathc;

	if (!cpu_count) {
		/* Maybe we should check this? */
		pr_err("no CPUs found - is /sys mounted?\n");
		goto out;
	}

	cpus = calloc(1, sizeof(cpus_t));
	if (!cpus)
		goto out;

	cpus->count = (uint32_t)cpu_count;
	cpus->cpus = calloc(cpu_count, sizeof(cpu_t));
	if (!cpus->cpus) {
		free(cpus);
		cpus = NULL;
		goto out;
	}

	for (i = 0; i < cpus->count; i++) {
		char *contents = NULL;
		cpu_t *cpu;

		cpu = &cpus->cpus[i];
		cpu->num = i;

		if (i == 0) {
			/* 1st CPU cannot be taken offline */
			cpu->online = 1;
		} else {
			len = strlen(results[i]);
			strncpy(path, results[i], len);
			MK_PATH(path, len, "/online");

			contents = get_string_from_file(path);
			if (!contents)
				goto out;
			cpu->online = atoi(contents);
			free(contents);
		}

		ret = get_cpu_cache_details(&cpus->cpus[i], results[i]);
		if (ret != EXIT_SUCCESS) {
			free(cpus->cpus);
			free(cpus);
			cpus = NULL;
			goto out;
		}
	}

out:
	globfree(&globbuf);

	return cpus;
}

/*
 * free_cpu_caches()
 * @cpus: value returned by get_all_cpu_cache_details().
 *
 * Undo the action of get_all_cpu_cache_details() by freeing all
 * associated resources.
 */
void free_cpu_caches(cpus_t *cpus)
{
	uint32_t  i;

	if (!cpus)
		return;

	for (i = 0; i < cpus->count; i++) {
		cpu_t *cpu = &cpus->cpus[i];

		free(cpu->caches);
	}
	free(cpus->cpus);
	free(cpus);
}

#endif
