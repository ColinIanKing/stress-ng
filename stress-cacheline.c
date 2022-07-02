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
	{ NULL,	"cacheline N",		"start N workers that exercise cachelines" },
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

static uint64_t get_L1_line_size(const stress_args_t *args)
{
#if defined(__linux__)
	stress_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;
	uint64_t cache_size = DEFAULT_L1_SIZE;

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
} while (0)

static int stress_cacheline_child(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	const size_t cacheline_size = g_shared->cacheline_size;
	volatile uint8_t *cacheline = (volatile uint8_t *)g_shared->cacheline;
	volatile uint8_t *data8 = cacheline + index;
	volatile uint8_t *aligned_cacheline = (volatile uint8_t *)
		((intptr_t)cacheline & ~(l1_cacheline_size - 1));
	static uint8_t tmp = 0xa5;
	register uint8_t val8;
	volatile uint16_t *data16;
	volatile uint32_t *data32;
	volatile uint64_t *data64;
#if defined(HAVE_INT128_T)
        volatile __uint128_t *data128;
#endif
	ssize_t i;
	int rc = EXIT_SUCCESS;

	do {
		*(data8) = tmp;
		EXERCISE((*data8));
		val8 = tmp;
		EXERCISE(val8);
		if (val8 != *data8) {
			pr_fail("%s: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			rc = EXIT_FAILURE;
		}
		tmp = val8;

		(*data8)++;
		/* 2 byte reads from same location */
		data16 = (uint16_t *)(((uintptr_t)data8) & ~(uintptr_t)1);
		(void)*(data16);
		shim_mb();

		/* 4 byte reads from same location */
		data32 = (uint32_t *)(((uintptr_t)data8) & ~(uintptr_t)3);
		(void)*(data32);
		shim_mb();

		/* 8 byte reads from same location */
		data64 = (uint64_t *)(((uintptr_t)data8) & ~(uintptr_t)7);
		(void)*(data64);
		shim_mb();

#if defined(HAVE_INT128_T)
		/* 116 byte reads from same location */
		data128 = (__uint128_t *)(((uintptr_t)data8) & ~(uintptr_t)15);
		(void)*(data128);
		shim_mb();
#endif

		(*data8)++;
		/* read cache line backwards */
		for (i = (ssize_t)cacheline_size - 8; i >= 0; i -= 8) {
			data64 = (uint64_t *)(aligned_cacheline + i);
			(void)*data64;
		}

		(*data8)++;
		/* read cache line forwards */
		for (i = 0; i < (ssize_t)cacheline_size; i += 8) {
			data64 = (uint64_t *)(aligned_cacheline + i);
			(void)*data64;
		}

		val8 = *(data8);
		(*data8)++;
		(*data8)++;
		(*data8)++;
		(*data8)++;
		(*data8)++;
		(*data8)++;
		(*data8)++;
		val8 += 7;

		if (*data8 != val8) {
			pr_fail("%s: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			rc = EXIT_FAILURE;
		}

		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;
		(void)*data8;
		*data8 = *data8;

		for (i = 0; i < 8; i++) {
			(void)*(data8);

			val8 = 1U << i;
			*data8 = val8;
			if (*data8 != val8) {
				pr_fail("%s: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
					args->name, index, val8, *data8);
				rc = EXIT_FAILURE;
			}
			val8 ^= 0xff;
			*data8 = val8;
			if (*data8 != val8) {
				pr_fail("%s: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
					args->name, index, val8, *data8);
				rc = EXIT_FAILURE;
			}
		}

		if (parent)
			inc_counter(args);
	} while (keep_stressing(args));

	/* Child tell parent it has finished */
	if (!parent)
		(void)kill(getppid(), SIGALRM);

	return rc;
}

/*
 *  stress_cacheline()
 *	execise a cacheline by multiple processes
 */
static int stress_cacheline(const stress_args_t *args)
{
	size_t l1_cacheline_size = (size_t)get_L1_line_size(args);
	const int index = (int)(args->instance * 2);
	pid_t pid;
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	if (args->instance == 0) {
		pr_dbg("%s: L1 cache line size %" PRIu64 " bytes\n", args->name, l1_cacheline_size);

		if ((args->num_instances * 2) < l1_cacheline_size) {
			pr_inf("%s: to fully exercise a %zd byte cache line, %zd instances are required\n",
				args->name, l1_cacheline_size, l1_cacheline_size / 2);
		}
	}
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(errno))
			goto again;
		if (!keep_stressing(args))
			goto finish;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		_exit(stress_cacheline_child(args, index + 1, false, l1_cacheline_size));
	} else {
		int status;

		stress_cacheline_child(args, index, true, l1_cacheline_size);

		(void)kill(pid, SIGALRM);
		(void)shim_waitpid(pid, &status, 0);

		if (WIFEXITED(status) && (WEXITSTATUS(status) != EXIT_SUCCESS))
			rc = WEXITSTATUS(status);
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

stressor_info_t stress_cacheline_info = {
	.stressor = stress_cacheline,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.help = help
};
