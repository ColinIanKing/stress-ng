/*
 * Copyright (C) 2016-2021 Canonical, Ltd.
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
#include "core-put.h"

#define MIN_PREFETCH_L3_SIZE      (4 * KB)
#define MAX_PREFETCH_L3_SIZE      (MAX_MEM_LIMIT)
#define DEFAULT_PREFETCH_L3_SIZE  (4 * MB)

#define STRESS_PREFETCH_OFFSETS	(128)
#define STRESS_CACHE_LINE_SIZE	(64)


static const stress_help_t help[] = {
	{ NULL,	"prefetch N" ,		"start N workers exercising memory prefetching " },
	{ NULL,	"prefetch-l3-size N",	"specify the L3 cache size of the CPU" },
	{ NULL,	"prefetch-ops N",	"stop after N bogo prefetching operations" },
	{ NULL,	NULL,                   NULL }
};

typedef struct {
	size_t	offset;
	uint64_t count;
	double	duration;
	double 	bytes;
	double 	rate;
} stress_prefetch_info_t;

static int stress_set_prefetch_L3_size(const char *opt)
{
	uint64_t stream_L3_size;
	size_t sz;

	stream_L3_size = stress_get_uint64_byte(opt);
	stress_check_range_bytes("stream-L3-size", stream_L3_size,
		MIN_PREFETCH_L3_SIZE, MAX_PREFETCH_L3_SIZE);
	sz = (size_t)stream_L3_size;

	return stress_set_setting("stream-L3-size", TYPE_ID_SIZE_T, &sz);
}

static inline uint64_t get_prefetch_L3_size(const stress_args_t *args)
{
	uint64_t cache_size = DEFAULT_PREFETCH_L3_SIZE;
#if defined(__linux__)
	stress_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint16_t max_cache_level;

	cpu_caches = stress_get_all_cpu_cache_details();
	if (!cpu_caches) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache details\n", args->name);
		return cache_size;
	}
	max_cache_level = stress_get_max_cache_level(cpu_caches);
	if ((max_cache_level > 0) && (max_cache_level < 3) && (!args->instance))
		pr_inf("%s: no L3 cache, using L%" PRIu16 " size instead\n",
			args->name, max_cache_level);

	cache = stress_get_cpu_cache(cpu_caches, max_cache_level);
	if (!cache) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as no suitable "
				"cache found\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		return cache_size;
	}
	if (!cache->size) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache size\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		return cache_size;
	}
	cache_size = cache->size;

	stress_free_cpu_caches(cpu_caches);
#else
	if (!args->instance)
		pr_inf("%s: using built-in defaults as unable to "
			"determine cache details\n", args->name);
#endif
	return cache_size;
}

static inline void OPTIMIZE3 stress_prefetch_benchmark(
	stress_prefetch_info_t *prefetch_info,
	const size_t i,
	uint64_t *RESTRICT l3_data,
	uint64_t *RESTRICT l3_data_end,
	uint64_t *total_count)
{
	double t1, t2, t3, t4;
	const size_t l3_data_size = (uintptr_t)l3_data_end - (uintptr_t)l3_data;
	volatile uint64_t *ptr;
	uint64_t *pre_ptr;

	shim_cacheflush((char *)l3_data, (int)l3_data_size, SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	__builtin___clear_cache((void *)l3_data, (void *)l3_data_end);
#endif

	/* Benchmark loop */
	ptr = l3_data;
	pre_ptr = l3_data + prefetch_info[i].offset;
	t1 = stress_time_now();
	while (ptr < l3_data_end) {
		ptr += 8;
		pre_ptr += 8;
		shim_mb();
	}
	t2 = stress_time_now();
	stress_void_ptr_put((volatile void *)ptr);
	stress_void_ptr_put((volatile void *)pre_ptr);

	/* Benchmark reads */
	if (prefetch_info[i].offset == 0) {
		/* Benchmark no prefetch */
		t3 = stress_time_now();
		ptr = l3_data;
		pre_ptr = l3_data + prefetch_info[i].offset;

		while (ptr < l3_data_end) {
			(void)(*(ptr + 0));
			(void)(*(ptr + 1));
			(void)(*(ptr + 2));
			(void)(*(ptr + 3));
			(void)(*(ptr + 4));
			(void)(*(ptr + 5));
			(void)(*(ptr + 6));
			(void)(*(ptr + 7));
			ptr += 8;
			pre_ptr += 8;
			shim_mb();
		}
		stress_void_ptr_put(pre_ptr);
	} else {
		/* Benchmark prefetch */
		t3 = stress_time_now();
		ptr = l3_data;
		pre_ptr = l3_data + prefetch_info[i].offset;

		while (ptr < l3_data_end) {
			(void)(*(ptr + 0));
			(void)(*(ptr + 1));
			(void)(*(ptr + 2));
			(void)(*(ptr + 3));
			(void)(*(ptr + 4));
			(void)(*(ptr + 5));
			(void)(*(ptr + 6));
			(void)(*(ptr + 7));
			shim_builtin_prefetch(pre_ptr, 0, 3);
			ptr += 8;
			pre_ptr += 8;
			shim_mb();
		}
	}
	t4 = stress_time_now();

	/* Update stats */
	prefetch_info[i].bytes += (double)l3_data_size;
	prefetch_info[i].duration += (t4 - t3) - (t2 - t1);
	prefetch_info[i].count++;
	(*total_count)++;
}

/*
 *  stress_prefetch()
 *	stress cache/memory/CPU with stream stressors
 */
static int stress_prefetch(const stress_args_t *args)
{
	uint64_t *l3_data, *l3_data_end, total_count = 0;
	size_t l3_data_size = 0, l3_data_mmap_size;
	stress_prefetch_info_t prefetch_info[STRESS_PREFETCH_OFFSETS];
	size_t i, best;
	double best_rate, ns;

	(void)stress_get_setting("stream-L3-size", &l3_data_size);
	if (l3_data_size == 0)
		l3_data_size = get_prefetch_L3_size(args);

	l3_data_mmap_size = l3_data_size + (STRESS_PREFETCH_OFFSETS * STRESS_CACHE_LINE_SIZE);

	l3_data = (uint64_t *)mmap(NULL, l3_data_mmap_size,
		PROT_READ | PROT_WRITE,
#if defined(MAP_POPULATE)
		MAP_POPULATE |
#endif
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (l3_data == MAP_FAILED) {
		pr_err("%s: cannot allocate %zu bytes\n",
			args->name, l3_data_mmap_size);
		return EXIT_NO_RESOURCE;
	}

	l3_data_end = (uint64_t *)((uintptr_t)l3_data + l3_data_size);

	(void)memset(l3_data, 0xa5, l3_data_mmap_size);

	for (i = 0; i < SIZEOF_ARRAY(prefetch_info); i++) {
		prefetch_info[i].offset = i * STRESS_CACHE_LINE_SIZE;
		prefetch_info[i].count = 0;
		prefetch_info[i].duration = 0.0;
		prefetch_info[i].bytes = 0.0;
		prefetch_info[i].rate = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		for (i = 0; i < SIZEOF_ARRAY(prefetch_info); i++) {
			stress_prefetch_benchmark(prefetch_info, i,
				l3_data, l3_data_end, &total_count);
		}
		inc_counter(args);
	} while (keep_stressing(args));

	best = 0;
	best_rate = 0.0;
	for (i = 0; i < SIZEOF_ARRAY(prefetch_info); i++) {
		if (prefetch_info[i].duration > 0.0)
			prefetch_info[i].rate = prefetch_info[i].bytes / prefetch_info[i].duration;
		else
			prefetch_info[i].rate = 0.0;

		if (prefetch_info[i].rate > best_rate) {
			best_rate = prefetch_info[i].rate;
			best = i;
		}
	}
	pr_inf("%s: using a %zd KB L3 cache, %" PRIu64 " benchmark rounds\n",
		args->name, l3_data_size >> 10, total_count);
	pr_inf("%s: non-prefetch read rate @ %.2f GB per sec\n",
		args->name, prefetch_info[0].rate / (double)GB);

	if (best_rate > 0.0)
		ns = 1000000000.0 * (double)prefetch_info[best].offset / best_rate;
	else
		ns = 0.0;

	pr_inf("%s: best prefetch read rate @ %.2f GB per sec at offset %zd (~%.2f nanoseconds)\n",
		args->name, best_rate / (double)GB,
		prefetch_info[best].offset, ns);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)l3_data, l3_data_mmap_size);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_prefetch_l3_size,	stress_set_prefetch_L3_size },
	{ 0,			NULL }
};

stressor_info_t stress_prefetch_info = {
	.stressor = stress_prefetch,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
