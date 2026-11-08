/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-madvise.h"
#include "core-mmap.h"

static const stress_help_t help[] = {
	{ NULL, "readpages",          "start N workers reading their readable pages" },
	{ NULL,	"readpages-ops N",    "stop after N bogo read operations" },
	{ NULL, "readpages-prefetch", "prefetch data (maybe a no-op on some systems" },
	{ NULL,	NULL,                 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_readpages_prefetch, "readpages-prefetch", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(__linux__)

typedef struct stress_readpages_map {
	uintptr_t begin;	/* start of mapping */
	uintptr_t end;		/* end of mapping */
	size_t	  pages;	/* size of mapping in pages */
} stress_readpages_map_t;

typedef void (*readpages_func_t)(const stress_readpages_map_t *map);

#define PREFETCH_OFFSET		(128)
#define PREFETCH_RD		(0)
#define PREFETCH_LOCALIITY	(3)

#define PTR_OFFSET(ptr)		(void *)(((uintptr_t)ptr) + PREFETCH_OFFSET)

#if defined(HAVE_INT128_T)

/*
 *  stress_readpages_mapping_prefetch()
 *	read mapped pages between begin and end with prefetching
 */
static void OPTIMIZE3 stress_readpages_mapping_prefetch(const stress_readpages_map_t *map)
{
	volatile __uint128_t *ptr;

	for (ptr = (__uint128_t *)map->begin; ptr < (__uint128_t *)map->end;) {
		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;
	}
}

/*
 *  stress_readpages_mapping()
 *	read mapped pages between begin and end
 */
static void OPTIMIZE3 stress_readpages_mapping(const stress_readpages_map_t *map)
{
	volatile __uint128_t *ptr;

	for (ptr = (__uint128_t *)map->begin; ptr < (__uint128_t *)map->end;) {
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		ptr += 4;
	}
}
#else

/*
 *  stress_readpages_mapping_prefetch()
 *	read mapped pages between begin and end with prefetching
 */
static void OPTIMIZE3 stress_readpages_mapping_prefetch(const stress_readpages_map_t *map)
{
	volatile uint64_t *ptr;

	for (ptr = (uint64_t *)map->begin; ptr < (uint64_t *)map->end;) {
		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		shim_builtin_prefetch(PTR_OFFSET(ptr), PREFETCH_RD, PREFETCH_LOCALIITY);
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;
	}
}

/*
 *  stress_readpages_mapping()
 *	read mapped pages between begin and end
 */
static void OPTIMIZE3 stress_readpages_mapping(const stress_readpages_map_t *map)
{
	volatile uint64_t *ptr;

	for (ptr = (uint64_t *)map->begin; ptr < (uint64_t *)map->end;) {
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;

		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);
		ptr += 8;
	}
}
#endif

/*
 *  stress_readpages_ro_page()
 *	get a read-only page mapping
 */
static int stress_readpages_ro_page(
	const char *buffer,
	const size_t page_size,
	stress_readpages_map_t *map)
{
	char tmppath[1024];
	char prot[6];
	uint64_t offset;
	const uintmax_t max_addr = UINTMAX_MAX - (UINTMAX_MAX >> 1);
	uint64_t begin;
	uint64_t end;
	int64_t  pages;
	const size_t max_pages = 0x80000000UL / page_size;

	tmppath[0] = '\0';
	if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		   " %5s %" SCNx64 " %*x:%*x %*d %1023s",
		   &begin, &end, prot, &offset, tmppath) != 5) {
		return -1;
	}

	/* Avoid non read-only pages */
	if (prot[0] != 'r')
		return -1;

	/* Avoid vdso and vvar */
	if (shim_strncmp("[v", tmppath, 2) == 0)
		return -1;

	/* Ignore bad ranges */
	if ((begin >= end) || (begin == 0) || (end == 0) || (end >= max_addr))
		return -1;

	pages = (int64_t)(end / page_size) - (int64_t)(begin / page_size);
	map->pages = (size_t)pages;

	/* Skip invalid or huge ranges */
	if ((map->pages == 0) || (map->pages > max_pages))
		return -1;
	map->begin = (uintptr_t)begin;
	map->end = (uintptr_t)end;

	return 0;
}

/*
 *  stress_readpages_maps_get()
 *  	get process space read-only mappings
 */
static size_t stress_readpages_maps_get(
	const size_t page_size,
	stress_readpages_map_t **maps)
{
	stress_readpages_map_t *new_maps;
	FILE *fp;
	char buffer[1024];
	size_t n_maps;
	size_t i;

	if (!maps)
		return 0;

	*maps = NULL;

	fp = fopen("/proc/self/maps", "r");
	if (!fp)
		return 0;

	for (n_maps = 0; fgets(buffer, sizeof(buffer), fp);) {
		stress_readpages_map_t map;

		if (stress_readpages_ro_page(buffer, page_size, &map) == 0)
			n_maps++;
	}
	if (n_maps == 0) {
		(void)fclose(fp);
		return 0;
	}
	new_maps = calloc(n_maps, sizeof(*new_maps));
	if (!new_maps) {
		(void)fclose(fp);
		return 0;
	}
	rewind(fp);
	for (i = 0; ((i < n_maps) && fgets(buffer, sizeof(buffer), fp));) {
		if (stress_readpages_ro_page(buffer, page_size, &new_maps[i]) == 0)
			i++;
	}
	(void)fclose(fp);
	*maps = new_maps;

	return n_maps;
}

/*
 *  stress_readpages_maps()
 */
static size_t OPTIMIZE3 stress_readpages_maps(
	readpages_func_t readpages_func,
	const stress_readpages_map_t *maps,
	const size_t n_maps)
{
	size_t n_pages = 0;
	register size_t i;

	for (i = 0; i < n_maps; i++) {
		readpages_func(&maps[i]);
		n_pages += maps[i].pages;
	}
	return n_pages;
}

/*
 *  stress_readpages()
 *	stress by forking and exiting
 */
static int stress_readpages(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	double total_pages = 0.0;
	double duration;
	double t;
	double rate;
	char msg[64];
	const size_t data_size = 4 * STRESS_MB;
	bool readpages_prefetch = false;
	readpages_func_t readpages_func;
	stress_readpages_map_t *maps = NULL;
	void *data;
	size_t n_maps;
	size_t max_pages = 0;
	const pid_t pid = getpid();
	int rc = EXIT_SUCCESS;
	int advice[3];
	int idx;
	int i;

	n_maps = stress_readpages_maps_get(page_size, &maps);
	if (n_maps == 0) {
		pr_inf_skip("%s: failed to read process mappings, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_setting_get("readpages-prefetch", &readpages_prefetch);
	readpages_func = readpages_prefetch ?
				stress_readpages_mapping_prefetch :
				stress_readpages_mapping;

	data = stress_mmap_populate(NULL, data_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data != MAP_FAILED) {
		stress_mmap_set(data, data_size, page_size);
		if (stress_instance_zero(args))
			stress_memory_usage_get(args, data_size, data_size * args->instances);
	}

	idx = 0;
#if defined(MADV_HUGEPAGE)
	advice[idx++] = MADV_HUGEPAGE;
#endif
#if defined(MADV_COLLAPSE)
	advice[idx++] = MADV_COLLAPSE;
#endif
#if defined(MADV_POPULATE_READ)
	advice[idx++] = MADV_POPULATE_READ;
#endif
	for (i = 0; i < idx; i++)
		stress_madvise_pid_all_pages(pid, &advice[i], 1);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	t = stress_time_now();
	do {
		const size_t pages = stress_readpages_maps(readpages_func, maps, n_maps);

		total_pages += (double)pages;
		if (pages > max_pages)
			max_pages = pages;
		stress_bogo_inc(args);
	} while (stress_continue(args));
	duration = stress_time_now() - t;

	rate = (duration > 0.0) ? total_pages / duration : 0.0;
	(void)snprintf(msg, sizeof(msg), "%zuK pages read per second", page_size >> 10);
	stress_metrics_set(args, msg, rate, STRESS_METRIC_GEOMETRIC_MEAN);
	rate *= (double)page_size;
	stress_metrics_set(args, "MB read per second", rate / (double)STRESS_MB, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "unique pages were read", (double)max_pages, STRESS_METRIC_GEOMETRIC_MEAN);

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (data != MAP_FAILED)
		(void)munmap(data, data_size);

	free(maps);

	return rc;
}

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("d-cache-read-miss"),
	STRESS_EX_FEATURE("memory-bound"),
	STRESS_EX_FEATURE("memory-bus"),
	STRESS_EX_FEATURE("memory-loads"),
	STRESS_EX_FEATURE("tlb"),

	STRESS_EX_END,
};

const stressor_info_t stress_readpages_info = {
	.stressor = stress_readpages,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.opts = opts,
	.exercises = exercises,
};

#else

const stressor_info_t stress_readpages_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_VM,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without linux /proc/self/maps",
};

#endif
