/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-cache.h"
#include "core-capabilities.h"
#include "core-target-clones.h"

static const stress_help_t help[] = {
	{ NULL,	"llc-affinity N",	"start N workers exercising low level cache over all CPUs" },
	{ NULL,	"llc-affinity-ops N",	"stop after N low-level-cache bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if defined(HAVE_SCHED_SETAFFINITY)

typedef void (*cache_line_func_t)(
        uint64_t *buf,
        uint64_t *buf_end,
        double *duration,
        const size_t cache_line_size);

static void stress_llc_size(size_t *llc_size, size_t *cache_line_size)
{
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
}

static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64(
	uint64_t *buf,
	uint64_t *buf_end,
	double *duration,
	const size_t cache_line_size)
{
	double t1, t2;
	static uint64_t val = 0;
	register uint64_t *ptr;

	(void)cache_line_size;

	t1 = stress_time_now();
	for (ptr = buf; ptr < buf_end; ptr += 8, val++) {
		*(ptr + 0) = val;
		*(ptr + 1) = val;
		*(ptr + 2) = val;
		*(ptr + 3) = val;
		*(ptr + 4) = val;
		*(ptr + 5) = val;
		*(ptr + 6) = val;
		*(ptr + 7) = val;

	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}

static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n(
	uint64_t *buf,
	uint64_t *buf_end,
	double *duration,
	const size_t cache_line_size)
{
	double t1, t2;
	static uint64_t val = 0;
	register uint64_t *ptr;
	const size_t n = cache_line_size / sizeof(uint64_t);

	t1 = stress_time_now();
	for (ptr = buf; ptr < buf_end; ptr += n, val++) {
		register uint64_t *cptr, *cptr_end;

		for (cptr = ptr, cptr_end = ptr + n; cptr < cptr_end; cptr++)
			*cptr = val;
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}

static void TARGET_CLONES OPTIMIZE3 stress_llc_read_cache_line_64(
	uint64_t *buf,
	uint64_t *buf_end,
	double *duration,
	const size_t cache_line_size)
{
	double t1, t2;
	register volatile uint64_t *ptr;

	(void)cache_line_size;

	t1 = stress_time_now();
	for (ptr = buf; ptr < buf_end; ptr += 8) {
		(void)*(ptr + 0);
		(void)*(ptr + 1);
		(void)*(ptr + 2);
		(void)*(ptr + 3);
		(void)*(ptr + 4);
		(void)*(ptr + 5);
		(void)*(ptr + 6);
		(void)*(ptr + 7);

	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}

static void TARGET_CLONES OPTIMIZE3 stress_llc_read_cache_line_n(
	uint64_t *buf,
	uint64_t *buf_end,
	double *duration,
	const size_t cache_line_size)
{
	double t1, t2;
	register uint64_t *ptr;
	const size_t n = cache_line_size / sizeof(uint64_t);

	t1 = stress_time_now();
	for (ptr = buf; ptr < buf_end; ptr += n) {
		register uint64_t *cptr, *cptr_end;

		for (cptr = ptr, cptr_end = ptr + n; cptr < cptr_end; cptr++)
			(void)*(volatile uint64_t *)cptr;
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}

/*
 *  stress_llc_affinity()
 *	stress the Lower Level Cache (LLC) while changing CPU affinity
 */
static int stress_llc_affinity(const stress_args_t *args)
{
	const int32_t max_cpus = stress_get_processors_configured();
	const size_t page_size = args->page_size;
	int32_t cpu = (int32_t)args->instance;
	size_t llc_size, cache_line_size, mmap_sz;
	uint64_t *buf, *buf_end;
	uint64_t affinity_changes = 0;
	double write_duration, read_duration, rate, writes, reads, t_start, duration;
	cache_line_func_t write_func, read_func;

	stress_llc_size(&llc_size, &cache_line_size);
	if (llc_size == 0) {
		pr_inf_skip("%s: cannot determine cache details, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}
	if (!args->instance) {
		pr_inf("%s: system has %zu KB LLC cache\n",
			args->name, llc_size / 1024);
	}

	mmap_sz = STRESS_MAXIMUM(max_cpus * page_size, llc_size);

	/*
	 *  Allocate a LLC sized buffer to exercise
	 */
	buf = (uint64_t *)mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, 0, 0);
	if (buf == MAP_FAILED) {
		pr_fail("%s: mmap'd region of %zu bytes failed\n", args->name, mmap_sz);
		return EXIT_NO_RESOURCE;
	}
	buf_end = (uint64_t *)((uintptr_t)buf + mmap_sz);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);


	writes = 0.0;
	write_duration = 0.0;
	reads = 0.0;
	read_duration = 0.0;

	if (cache_line_size == 64) {
		write_func = stress_llc_write_cache_line_64;
		read_func = stress_llc_read_cache_line_64;
	} else {
		write_func = stress_llc_write_cache_line_n;
		read_func = stress_llc_read_cache_line_n;
	}

	t_start = stress_time_now();
	do {
		int32_t i;

		for (i = 0; keep_stressing_flag() && (i < max_cpus); i++) {
			const int32_t set_cpu = (cpu + i) % max_cpus;
			cpu_set_t mask;

			CPU_ZERO(&mask);
			CPU_SET(set_cpu, &mask);

			if (sched_setaffinity(0, sizeof(mask), &mask) == 0)
				affinity_changes++;

			read_func(buf, buf_end, &read_duration, cache_line_size);
			reads += (double)mmap_sz;

			write_func(buf, buf_end, &write_duration, cache_line_size);
			writes += (double)mmap_sz;
		}
		inc_counter(args);
	} while (keep_stressing(args));

	duration = stress_time_now() - t_start;

	writes /= (double)MB;
	rate = write_duration > 0.0 ? (double)writes / write_duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "write memory rate (MB per sec)", rate);
	reads /= (double)MB;
	rate = read_duration > 0.0 ? (double)reads / read_duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 1, "read memory rate (MB per sec)", rate);

	rate = duration > 0.0 ? (double)affinity_changes / duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 2, "CPU affinity changes/sec", rate);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)buf, mmap_sz);

	return EXIT_SUCCESS;
}

stressor_info_t stress_llc_affinity_info = {
	.stressor = stress_llc_affinity,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

stressor_info_t stress_llc_affinity_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#endif
