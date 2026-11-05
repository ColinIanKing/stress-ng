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

typedef void (*readpages_func_t)(const uintptr_t begin, const uintptr_t end);

#define PREFETCH_OFFSET		(128)
#define PREFETCH_RD		(0)
#define PREFETCH_LOCALIITY	(3)

#define PTR_OFFSET(ptr)		(void *)(((uintptr_t)ptr) + PREFETCH_OFFSET)

#if defined(HAVE_INT128_T)

/*
 *  stress_readpages_mapping_prefetch()
 *	read mapped pages between begin and end with prefetching
 */
static void OPTIMIZE3 stress_readpages_mapping_prefetch(
	const uintptr_t begin,
	const uintptr_t end)
{
	volatile __uint128_t *ptr;

	for (ptr = (__uint128_t *)begin; ptr < (__uint128_t *)end;) {
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
static void OPTIMIZE3 stress_readpages_mapping(
	const uintptr_t begin,
	const uintptr_t end)
{
	volatile __uint128_t *ptr;

	for (ptr = (__uint128_t *)begin; ptr < (__uint128_t *)end;) {
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
static void OPTIMIZE3 stress_readpages_mapping_prefetch(
	const uintptr_t begin,
	const uintptr_t end)
{
	volatile uint64_t *ptr;

	for (ptr = (uint64_t *)begin; ptr < (uint64_t *)end;) {
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
static void OPTIMIZE3 stress_readpages_mapping(
	const uintptr_t begin,
	const uintptr_t end)
{
	volatile uint64_t *ptr;

	for (ptr = (uint64_t *)begin; ptr < (uint64_t *)end;) {
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
 *  stress_readpages_maps()
 */
static double OPTIMIZE3 stress_readpages_maps(
	readpages_func_t readpages_func,
	const size_t page_size)
{
	char buffer[4096];
	FILE *fp;
	const uintmax_t max_addr = UINTMAX_MAX - (UINTMAX_MAX >> 1);
	double n_pages = 0.0;
	const double d_page_size = (double)page_size;

	fp = fopen("/proc/self/maps", "r");
	if (!fp)
		return n_pages;

	/*
	 * Look for field 0060b000-0060c000 r--p 0000b000 08:01 1901726
	 */
	while (fgets(buffer, sizeof(buffer), fp)) {
		uint64_t begin;
		uint64_t end;
		uint64_t offset;
		double len;
		char tmppath[1024];
		char prot[6];

		tmppath[0] = '\0';
		if (sscanf(buffer, "%" SCNx64 "-%" SCNx64
		           " %5s %" SCNx64 " %*x:%*x %*d %1023s", &begin, &end, prot, &offset, tmppath) != 5) {
			continue;
		}

		if (prot[0] != 'r')
			continue;

		/* Avoid vdso and vvar */
		if (shim_strncmp("[v", tmppath, 2) == 0)
			continue;

		if ((begin > UINTPTR_MAX) || (end > UINTPTR_MAX))
			continue;

		/* Ignore bad ranges */
		if ((begin >= end) || (begin == 0) || (end == 0) || (end >= max_addr))
			continue;

		len = (double)(end - begin);

		/* Skip invalid ranges */
		if ((len < d_page_size) || (len > (double)0x80000000UL))
			continue;

		readpages_func((uintptr_t)begin, (uintptr_t)end);
		n_pages += len / d_page_size;
	}
	(void)fclose(fp);

	return n_pages;
}

/*
 *  stress_readpages()
 *	stress by forking and exiting
 */
static int stress_readpages(stress_args_t *args)
{
	double total_pages = 0.0;
	double max_pages = 0.0;
	double duration;
	double t;
	double rate;
	char msg[64];
	const size_t data_size = 4 * STRESS_MB;
	bool readpages_prefetch = false;
	readpages_func_t readpages_func;
	void *data;
	const size_t page_size = args->page_size;
	const pid_t pid = getpid();
	int rc = EXIT_SUCCESS;
	int advice[3];
	int idx;
	int i;

	(void)stress_setting_get("readpages-prefetch", &readpages_prefetch);
	readpages_func = readpages_prefetch ?
				stress_readpages_mapping_prefetch :
				stress_readpages_mapping;

	data = stress_mmap_populate(NULL, data_size, PROT_READ | PROT_WRITE,
			MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (data != MAP_FAILED) {
		stress_mmap_set(data, data_size, page_size);
		stress_mmap_discontiguous(data, data_size);
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
		const double pages = stress_readpages_maps(readpages_func, page_size);

		total_pages += pages;
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
	stress_metrics_set(args, "unique pages were read", max_pages, STRESS_METRIC_GEOMETRIC_MEAN);

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	if (data != MAP_FAILED)
		(void)munmap(data, data_size);

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
