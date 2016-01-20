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

#if defined(__linux__)

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glob.h>
#include <limits.h>
#include <errno.h>
#include <sched.h>

#include "stress-ng.h"

/* append @element to array @path (which has len @len) */
#define mk_path(path, len, element) \
	memset((path)+len, '\0', sizeof(path) - len); \
strncpy((path)+len, element, strlen(element))

static struct generic_map cache_type_map[] = {
	{"data"        , CACHE_TYPE_DATA},
	{"instruction" , CACHE_TYPE_INSTRUCTION},
	{"unified"     , CACHE_TYPE_UNIFIED},

	{ NULL         , CACHE_TYPE_UNKNOWN}
};

static const char *get_cache_name(cache_type_t type)
	__attribute__((unused));

/**
 * @path: file to check.
 *
 * Determine if specified file exists.
 *
 * Returns: file type if @path exists, else 0.
 **/
static int
file_exists(const char *path)
{
	struct stat st;

	assert(path);

	if (stat(path, &st) < 0)
		return 0;

	return(st.st_mode & S_IFMT);
}

/**
 * @path: file to read.
 *
 * Reads the first word of @file, returning the value as a string.
 *
 * Returns: dynamically-allocated copy of the contents of @path,
 * or NULL on error.
 **/
static char *
get_contents(const char *path)
{
	FILE         *f = NULL;
	char         *contents = NULL;
	struct stat   st;
	size_t        size;

	assert(path);

	if (stat(path, &st) < 0)
		return NULL;

	size = st.st_size;

	contents = malloc(size);
	if (! contents)
		return NULL;

	f = fopen(path, "r");
	if (! f)
		goto err;

	if (! fgets(contents, size, f))
		goto err;

	fclose(f);
	return contents;

err:
	if (f)
		fclose(f);

	if (contents)
		free(contents);
	return NULL;
}

/*
 * Returns: dynamically-allocated copy of the contents of @path,
 * or NULL on error.
 */
static char *
get_string_from_file(const char *path)
{
	char   *s;
	size_t  len;

	s = get_contents(path);

	if (! s)
		return NULL;

	len = strlen(s);

	if (s[len-1] == '\n')
		s[len-1] = '\0';

	return s;
}

/*
 * Convert human-readable size ("32K", "4M", etc) into bytes.
 */
static size_t
size_to_bytes(const char *str)
{
	size_t              bytes;
	size_t              multiplier;
	unsigned long int   value;
	int                 ret;
	char               *s;

	assert(str);

	ret = sscanf(str, "%lu%ms", &value, &s);
	if (ret != 2) {
		return 0;
	}

	assert(s);

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
		pr_err(stderr, "unable to convert string to bytes: %s\n", str);
		bytes = 0;
		goto out;
	}

	bytes = value * multiplier;

out:
	free(s);

	return bytes;
}

/* Convert a human-readable cache type into a cache_type_t */
static cache_type_t
get_cache_type(const char *name)
{
	struct generic_map *p;

	assert(name);

	for (p = cache_type_map; p && p->name; p++) {
		if (! strcasecmp(p->name, name))
			return p->value;
	}

	return CACHE_TYPE_UNKNOWN;
}

/* Convert a cache_type_t to a human-readable cache type */
static const char *
get_cache_name(cache_type_t type)
{
	struct generic_map *p;

	assert(type);

	for (p = cache_type_map; p && p->name; p++) {
		if (p->value == type)
			return p->name;
	}

	return NULL;
}

/* "Fill in" @cache with details of the cache */
static int
add_cpu_cache_detail(cpu_cache_t *cache, const char *index_path)
{
	char     path[PATH_MAX] = {0};
	size_t   len;
	char    *contents = NULL;

	assert(cache);
	assert(index_path);

	len = strlen(index_path);

	strncpy(path, index_path, len);

	/*******************************/
	mk_path(path, len, "/type");

	contents = get_string_from_file(path);
	if (! contents)
		return EXIT_FAILURE;

	cache->type = (cache_type_t)get_cache_type(contents);
	assert(cache->type != CACHE_TYPE_UNKNOWN);
	free(contents);

	/*******************************/
	mk_path(path, len, "/size");

	contents = get_string_from_file(path);
	if (! contents)
		return EXIT_FAILURE;

	cache->size = size_to_bytes(contents);
	free(contents);

	/*******************************/
	mk_path(path, len, "/level");

	contents = get_string_from_file(path);
	if (! contents)
		return EXIT_FAILURE;

	cache->level = atoi(contents);
	free(contents);

	/*******************************/
	mk_path(path, len, "/coherency_line_size");

	contents = get_string_from_file(path);
	if (! contents)
		return EXIT_FAILURE;

	cache->line_size = atoi(contents);
	free(contents);

	/*******************************/
	mk_path(path, len, "/ways_of_associativity");

	contents = get_string_from_file(path);

	/* Don't error if file is not readable: cache may not be way-based
	*/
	cache->ways = contents ? atoi(contents) : 0;
	if (contents)
		free(contents);

	return EXIT_SUCCESS;
}

/*
 * POTENTIAL BUG: assumes only 1 data cache per CPU cache level.
 */
static cpu_cache_t *
get_cache_by_cpu(cpu_t *cpu, int cache_level)
{
	cpu_cache_t  *p;
	unsigned int  i;

	assert (cpu);
	assert (cache_level);

	for (i = 0; i < cpu->cache_count; i++) {
		p = &cpu->caches[i];

		if (p->level != cache_level)
			continue;

		/* we want a data cache */
		if (p->type != CACHE_TYPE_INSTRUCTION)
			return p;
	}

	return NULL;
}

cpu_cache_t *
get_cpu_cache(const cpus_t *cpus, int cache_level)
{
	cpu_t        *cpu;

	/* FIXME: should really determine current CPU index using
	 * sched_getcpu(3).
	 */
	int cpu_num = 0;

	assert(cpus);

	cpu = &cpus->cpus[cpu_num];

	if ((size_t)cache_level > cpu->cache_count) {
		pr_err(stderr, "no cache available at this level (try 1-%d)\n",
				(int)cpu->cache_count - 1);
		return NULL;
	}

	return get_cache_by_cpu(cpu, cache_level);
}

static int
get_cpu_cache_details(cpu_t *cpu, const char *cpu_path)
{
	size_t     i;
	size_t     len;
	int        ret;
	glob_t     globbuf = {0};
	char       glob_path[PATH_MAX] = {0};
	char     **results;

	assert(cpu);
	assert(cpu_path);

	len = strlen(cpu_path);
	strncat(glob_path, cpu_path, len);

	strncat(glob_path, GLOB_PATTERN_INDEX_PREFIX, sizeof(glob_path) - len - 1);
	ret = glob(glob_path, GLOB_ONLYDIR, NULL, &globbuf);

	if (ret != 0) {
		pr_err(stderr, "glob(%s)failed: %d\n", glob_path, ret);
		return EXIT_FAILURE;
	}

	results = globbuf.gl_pathv;
	cpu->cache_count = globbuf.gl_pathc;

	if (! cpu->cache_count) {
		pr_err(stderr, "no CPU caches found\n");
		goto err;
	}

	cpu->caches = calloc(cpu->cache_count, sizeof(cpu_cache_t));
	if (! cpu->caches) {
		size_t cache_bytes = cpu->cache_count * sizeof(cpu_cache_t);

		pr_err(stderr, "failed to allocate %lu bytes for cpu caches\n",
				(unsigned long int)cache_bytes);
		goto err;
	}

	for (i = 0; i < cpu->cache_count; i++) {
		ret = add_cpu_cache_detail(&cpu->caches[i], results[i]);
		if (ret != EXIT_SUCCESS)
			goto err;
	}

	globfree(&globbuf);

	/* reset */
	glob_path[0] = '\0';

	return EXIT_SUCCESS;

err:
	globfree(&globbuf);
	return EXIT_FAILURE;
}

cpus_t *
get_all_cpu_cache_details(void)
{
	size_t     i;
	int        ret;
	char       path[PATH_MAX] = {0};
	glob_t     globbuf = {0};
	char     **results;
	cpus_t    *cpus = NULL;
	size_t     cpu_count;
	size_t     len;

	ret = file_exists(SYS_CPU_PREFIX);
	if (! ret) {
		pr_err(stderr, "%s does not exist\n", SYS_CPU_PREFIX);
		return NULL;
	}

	if (ret != S_IFDIR) {
		pr_err(stderr, "file %s is not a directory\n", SYS_CPU_PREFIX);
		return NULL;
	}

	ret = glob(GLOB_PATTERN, GLOB_ONLYDIR, NULL, &globbuf);

	if (ret != 0) {
		pr_err(stderr, "glob failed: %d\n", ret);
		return NULL;
	}

	results = globbuf.gl_pathv;
	cpu_count = globbuf.gl_pathc;

	if (! cpu_count) {
		/* Maybe we should check this? */
		pr_err(stderr, "no CPUs found - is /sys mounted?\n");
		goto out;
	}

	cpus = calloc(1, sizeof(cpus_t));
	if (! cpus)
		goto out;

	cpus->count = cpu_count;
	cpus->cpus = calloc(cpu_count, sizeof(cpu_t));
	if (! cpus->cpus) {
		free(cpus);
		cpus = NULL;
		goto out;
	}

	for (i = 0; i < cpu_count; i++) {
		char *contents = NULL;
		cpu_t *cpu;

		cpu = &cpus->cpus[i];
		cpu->num = i;

		if (i == 0) {
			/* 1st CPU cannot be taken offline */
			cpu->online = 1;
		} else {
			len = strlen(results[i]);
			mk_path(path, len, "/online");

			contents = get_string_from_file(path);
			if (! contents)
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

void
free_cpu_caches(cpus_t *cpus)
{
	unsigned int  i;
	cpu_t        *cpu;

	if (! cpus)
		return;

	for (i = 0; i < cpus->count; i++) {
		cpu = &cpus->cpus[i];
		free(cpu->caches);
	}

	free(cpus->cpus);
	free(cpus);
	cpus = NULL;
}

#endif /* __linux__ */
