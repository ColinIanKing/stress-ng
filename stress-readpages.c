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
#include "core-sort.h"

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
#if defined(HAVE_SIGLONGJMP)
	volatile bool segv;	/* mapping READ tripped a SIGSEGV */
#endif
} stress_readpages_map_t;

typedef struct stress_streadpages_maps {
	stress_readpages_map_t	*maps;	/* array of maps */
	size_t			n_maps;	/* number of maps */
} stress_readpages_maps_t;

typedef void (*readpages_func_t)(const stress_readpages_map_t *map);

static volatile stress_readpages_map_t *stress_readpages_current_map;

#if defined(HAVE_SIGLONGJMP)
static volatile bool do_jmp = true;
static sigjmp_buf jmp_env;
#endif

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
static int stress_readpages_maps_get(
	const size_t page_size,
	volatile stress_readpages_maps_t *maps)
{
	FILE *fp;
	char buffer[1024];
	size_t n_maps;
	size_t i;

	maps->maps = NULL;
	maps->n_maps = 0;

	fp = fopen("/proc/self/maps", "r");
	if (!fp)
		return -1;

	for (n_maps = 0; fgets(buffer, sizeof(buffer), fp);) {
		stress_readpages_map_t map;

		if (stress_readpages_ro_page(buffer, page_size, &map) == 0)
			n_maps++;
	}
	if (n_maps == 0) {
		(void)fclose(fp);
		return -1;
	}
	maps->maps = calloc(n_maps, sizeof(*maps->maps));
	if (!maps->maps) {
		(void)fclose(fp);
		return -1;
	}
	rewind(fp);
	for (i = 0; ((i < n_maps) && fgets(buffer, sizeof(buffer), fp));) {
		if (stress_readpages_ro_page(buffer, page_size, &maps->maps[i]) == 0)
			i++;
	}
	(void)fclose(fp);
	maps->n_maps = i;

	return 0;
}

/*
 *  stress_readpages_maps()
 */
static size_t OPTIMIZE3 stress_readpages_maps(
	readpages_func_t readpages_func,
	const volatile stress_readpages_maps_t *maps)
{
	size_t n_pages = 0;
	register size_t i;

	for (i = 0; i < maps->n_maps; i++) {
		stress_readpages_map_t *map = &maps->maps[i];

#if defined(HAVE_SIGLONGJMP)
		if (map->segv)
			continue;
#endif
		stress_readpages_current_map = map;
		readpages_func(map);
		stress_readpages_current_map = NULL;

		n_pages += map->pages;
	}
	return n_pages;
}

STRESS_SHUFFLE(stress_readpages_map_shuffle, stress_readpages_map_t)

#if defined(HAVE_SIGLONGJMP)
/*
 *  stress_readpage_segv_handler()
 *  	handler for read-only mappings that caused a SIGSEGV
 */
static void stress_readpage_segv_handler(int signum)
{
	if (stress_readpages_current_map)
		stress_readpages_current_map->segv = true;

	stress_readpages_current_map = NULL;
	stress_signal_siglongjmp_flag(signum, jmp_env, 1, &do_jmp);
}
#endif

/*
 *  stress_readpages()
 *	stress by forking and exiting
 */
static int stress_readpages(stress_args_t *args)
{
#if defined(HAVE_SIGLONGJMP)
        struct sigaction old_action;
	size_t segv_mappings;
	int ret;
#endif
	const size_t page_size = args->page_size;
	double duration;
	stress_readpages_maps_t maps;
	CLOBBERED double total_pages = 0.0;
	CLOBBERED double t;
	double rate;
	char msg[64];
	const size_t data_size = 4 * STRESS_MB;
	bool readpages_prefetch = false;
	readpages_func_t readpages_func;
	void *data;
	CLOBBERED size_t max_pages = 0;
	const pid_t pid = getpid();
	CLOBBERED int rc = EXIT_SUCCESS;
	int advice[3];
	size_t idx;
	size_t i;

	maps.maps = NULL;
	maps.n_maps = 0;

	stress_readpages_current_map = NULL;
	stress_readpages_maps_get(page_size, &maps);
	if (maps.n_maps == 0) {
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
#if defined(HAVE_SIGLONGJMP)
	/* ret = 1 means we've hit a SIGSEGV, fall back into do loop */
        ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		if (!stress_continue(args))
			goto tidy;
	} else {
		if (stress_signal_handler(args->name, SIGSEGV, stress_readpage_segv_handler, &old_action) < 0) {
			rc = EXIT_NO_RESOURCE;
			goto tidy;
		}
        }
	do_jmp = true;
#endif

	do {
		const size_t pages = stress_readpages_maps(readpages_func, &maps);

		total_pages += (double)pages;
		if (pages > max_pages)
			max_pages = pages;
		stress_readpages_map_shuffle(maps.maps, maps.n_maps);
		stress_bogo_inc(args);
	} while (stress_continue(args));
	duration = stress_time_now() - t;

#if defined(HAVE_SIGLONGJMP)
	do_jmp = false;
	(void)stress_signal_restore(args->name, SIGSEGV, &old_action);
#endif

	rate = (duration > 0.0) ? total_pages / duration : 0.0;
	(void)snprintf(msg, sizeof(msg), "%zuK pages read per second", page_size >> 10);
	stress_metrics_set(args, msg, rate, STRESS_METRIC_GEOMETRIC_MEAN);
	rate *= (double)page_size;
	stress_metrics_set(args, "MB read per second", rate / (double)STRESS_MB, STRESS_METRIC_GEOMETRIC_MEAN);
	stress_metrics_set(args, "unique pages were read", (double)max_pages, STRESS_METRIC_GEOMETRIC_MEAN);

#if defined(HAVE_SIGLONGJMP)
	for (segv_mappings = 0, i = 0; i < maps.n_maps; i++) {
		segv_mappings += (int)maps.maps[i].segv;
	}
	rate = (maps.n_maps > 0) ? (100.0 * (double)segv_mappings) / (double)maps.n_maps : 0.0;
	stress_metrics_set(args, "% of mappings generated SIGSEGV", rate, STRESS_METRIC_HARMONIC_MEAN);
#endif

#if defined(HAVE_SIGLONGJMP)
tidy:
#endif
	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (data != MAP_FAILED)
		(void)munmap(data, data_size);

	free(maps.maps);

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
