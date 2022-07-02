/*
 * Copyright (C) 2022      Colin Ian King.
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

static const stress_help_t help[] = {
	{ NULL,	"cacheline N",		"start N workers that exercise a cacheline" },
	{ NULL,	"cacheline-ops N",	"stop after N cacheline bogo operations" },
	{ NULL,	NULL,		NULL }
};

#define DEFAULT_L1_SIZE		(64)

/*
 *  8 bit rotate right
 */
#define ROR8(val)				\
do {						\
	uint8_t tmp = (val);			\
	const uint8_t bit0 = (tmp & 1) << 7;	\
	tmp >>= 1;                             	\
	tmp |= bit0;                           	\
	(val) = tmp;                           	\
} while (0)

/*
 *  8 bit rotate left
 */
#define ROL8(val)				\
do {						\
	uint8_t tmp = (val);			\
	const uint8_t bit7 = (tmp & 0x80) >> 7;	\
	tmp <<= 1;                             	\
	tmp |= bit7;                           	\
	(val) = tmp;                           	\
} while (0)

static inline uint64_t get_L1_line_size(const stress_args_t *args)
{
	uint64_t cache_size = DEFAULT_L1_SIZE;
#if defined(__linux__)
	stress_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;

	cpu_caches = stress_get_all_cpu_cache_details();
	if (!cpu_caches) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache line details\n", args->name);
		return cache_size;
	}

	cache = stress_get_cpu_cache(cpu_caches, 1);
	if (!cache) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as no suitable "
				"cache found\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		return cache_size;
	}
	if (!cache->line_size) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache line size\n", args->name);
		stress_free_cpu_caches(cpu_caches);
		return cache_size;
	}
	cache_size = cache->line_size;

	stress_free_cpu_caches(cpu_caches);
#else
	if (!args->instance)
		pr_inf("%s: using built-in defaults as unable to "
			"determine cache line details\n", args->name);
#endif
	return cache_size;
}

#define EXERCISE(data)	\
do {			\
	(data)++;	\
	shim_mb();	\
	ROL8(data);	\
	shim_mb();	\
	ROR8(data);	\
	shim_mb();	\
	(data) *= 3;	\
	shim_mb();	\
	(data) ^= 0x01;	\
	shim_mb();	\
	(data) ^= 0x10;	\
	shim_mb();	\
	(data) ^= 0x02;	\
	shim_mb();	\
	(data) ^= 0x20;	\
	shim_mb();	\
	(data) ^= 0x04;	\
	shim_mb();	\
	(data) ^= 0x40;	\
	shim_mb();	\
	(data) ^= 0x08;	\
	shim_mb();	\
	(data) ^= 0x80;	\
	shim_mb();	\
	(data) ^= 0xff;	\
	shim_mb();	\
	ROR8(data);	\
	shim_mb();	\
} while (0)

static void stress_cacheline_child(
	const stress_args_t *args,
	const int instance,
	volatile uint8_t *cache_line,
	const size_t cache_line_size)
{
	volatile uint8_t *data = cache_line + instance;
	register uint8_t val;
	register size_t i;

	val = *(data);

	EXERCISE((*data));
	EXERCISE(val);

	if (val != *data) {
		pr_fail("%s: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
			args->name, instance, val, *data);
	}

	for (i = 0; i < cache_line_size; i++) {
		data[0] += cache_line[i];
		shim_mb();
	}

	*data = val;
	shim_mb();
}

/*
 *  stress_cacheline()
 *	execise a cacheline by multiple processes
 */
static int stress_cacheline(const stress_args_t *args)
{
	size_t cache_line_size = (size_t)get_L1_line_size(args);
	volatile uint8_t *cache_line;
	pid_t *pids;
	size_t i;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (args->instance == 0)
		pr_dbg("%s: L1 cache line_size %" PRIu64 " bytes\n", args->name, cache_line_size);

	if (cache_line_size > 256)
		cache_line_size = 256;

	cache_line = (uint8_t *)mmap(NULL, cache_line_size, PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (cache_line == MAP_FAILED) {
		pr_inf("%s: could not mmap cache line buffer, skipping stressor, errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	pids = calloc(cache_line_size, sizeof(*pids));
	if (!pids) {
		pr_inf("%s: could not alloc pids array, skipping stressor\n",
			args->name);
		(void)munmap((void *)cache_line, cache_line_size);
		return EXIT_NO_RESOURCE;
	}

	for (i = 1; i < cache_line_size; i++) {
		pids[i] = fork();
		if (pids[i] == 0) {
			do {
				stress_cacheline_child(args, i, cache_line, cache_line_size);
			} while (keep_stressing(args));
			_exit(0);
		}
	}

	do {
		stress_cacheline_child(args, 0, cache_line, cache_line_size);
		inc_counter(args);
	} while (keep_stressing(args));

	for (i = 1; i < cache_line_size; i++) {
		if (pids[i] > 0) {
			int status;

			(void)kill(pids[i], SIGKILL);
			(void)waitpid(pids[i], &status, 0);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	free(pids);
	(void)munmap((void *)cache_line, cache_line_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_cacheline_info = {
	.stressor = stress_cacheline,
	.class = CLASS_CPU,
	.help = help
};
