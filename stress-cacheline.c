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
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-killpid.h"
#include "core-lock.h"
#include "core-pragma.h"

#include <sched.h>

#define DEFAULT_L1_SIZE		(64)

#if defined(HAVE_ATOMIC_FETCH_ADD) &&	\
    defined(__ATOMIC_RELAXED)
#define SHIM_ATOMIC_INC(ptr)       \
	do { __atomic_fetch_add(ptr, 1, __ATOMIC_RELAXED); } while (0)
#endif

#define EXERCISE(data)		\
do {				\
	(data)++;		\
	stress_asm_mb();	\
	data = shim_rol8(data);	\
	stress_asm_mb();	\
	data = shim_ror8(data);	\
	stress_asm_mb();	\
} while (0)

static const stress_help_t help[] = {
	{ NULL,	"cacheline N",		"start N workers that exercise cachelines" },
	{ NULL,	"cacheline-affinity",	"modify CPU affinity" },
	{ NULL,	"cacheline-method M",	"use cacheline stressing method M" },
	{ NULL,	"cacheline-ops N",	"stop after N cacheline bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef int (*stress_cacheline_func)(
        stress_args_t *args,
        const int idx,
        const bool parent,
        const size_t l1_cacheline_size);

typedef struct {
	const char *name;
	const stress_cacheline_func	func;
} stress_cacheline_method_t;

static uint64_t get_L1_line_size(stress_args_t *args)
{
	uint64_t cache_size = DEFAULT_L1_SIZE;
#if defined(__linux__) ||	\
    defined(__APPLE__)
	stress_cpu_cache_cpus_t *cpu_caches;
	const stress_cpu_cache_t *cache = NULL;

	cpu_caches = stress_cpu_cache_get_all_details();
	if (!cpu_caches) {
		if (!args->instance)
			pr_inf("%s: using built-in defaults as unable to "
				"determine cache line details\n", args->name);
		return cache_size;
	}

	cache = stress_cpu_cache_get(cpu_caches, 1);
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

static int stress_cacheline_adjacent(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	register uint8_t val8 = *(data8);
	volatile uint8_t *data8adjacent = (volatile uint8_t *)(((uintptr_t)data8) ^ 1);

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		(*data8)++;
		(void)(*data8adjacent);
		stress_asm_mb();
		val8 += 7;

		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: adjacent method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_copy(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	const volatile uint8_t *data8adjacent = (volatile uint8_t *)(((uintptr_t)data8) ^ 1);

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		register uint8_t val8;

		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		(*data8) = (*data8adjacent);
		val8 = *data8;

		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: copy method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_inc(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		(*data8)++;
		stress_asm_mb();
		val8 += 7;

		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: inc method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdwr(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		uint8_t tmp;

		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();
		tmp = *data8;
		*data8 = tmp;
		stress_asm_mb();

		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: rdwr method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_mix(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)(uintptr_t)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	static uint8_t tmp = 0xa5;

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		register uint8_t val8;

		*(data8) = tmp;
		EXERCISE((*data8));
		val8 = tmp;
		EXERCISE(val8);
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: mix method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
		tmp = val8;
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdrev64(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	const ssize_t cacheline_size = (ssize_t)g_shared->cacheline.size;
	uintptr_t aligned_cacheline = (uintptr_t)buffer & ~(l1_cacheline_size - 1);

	(void)parent;
	(void)l1_cacheline_size;

	for (i = 0; i < 1024; i++) {
		register ssize_t j;
		uint8_t val8;

		(*data8)++;
		val8 = *data8;

		/* read cache line backwards */
PRAGMA_UNROLL
		for (j = cacheline_size - 8; j >= 0; j -= 8) {
			volatile uint64_t *data64 = (volatile uint64_t *)(aligned_cacheline + (size_t)j);

			(void)*data64;
			stress_asm_mb();
		}
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdrev64 method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdfwd64(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	const size_t cacheline_size = g_shared->cacheline.size;
	uintptr_t aligned_cacheline = (uintptr_t)buffer & ~(l1_cacheline_size - 1);

	(void)parent;
	(void)l1_cacheline_size;

	for (i = 0; i < 1024; i++) {
		register size_t j;
		uint8_t val8;

		(*data8)++;
		val8 = *data8;

		/* read cache line forwards */
#if !defined(HAVE_COMPILER_CLANG)
PRAGMA_UNROLL
#endif
		for (j = 0; j < cacheline_size; j += 8) {
			volatile uint64_t *data64 = (volatile uint64_t *)(aligned_cacheline + j);

			(void)*data64;
			stress_asm_mb();
		}
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdfwd64: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdints(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	volatile uint16_t *data16 = (uint16_t *)(((uintptr_t)data8) & ~(uintptr_t)1);
	volatile uint32_t *data32 = (uint32_t *)(((uintptr_t)data8) & ~(uintptr_t)3);
	volatile uint64_t *data64 = (uint64_t *)(((uintptr_t)data8) & ~(uintptr_t)7);
#if defined(HAVE_INT128_T)
        volatile __uint128_t *data128 = (__uint128_t *)(((uintptr_t)data8) & ~(uintptr_t)15);
#endif

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		uint8_t val8;

		/* 1 byte increment and read */
		(*data8)++;
		val8 = *data8;
		stress_asm_mb();

		/* 2 byte reads from same location */
		(void)*(data16);
		stress_asm_mb();

		/* 4 byte reads from same location */
		(void)*(data32);
		stress_asm_mb();

		/* 8 byte reads from same location */
		(void)*(data64);
		stress_asm_mb();

#if defined(HAVE_INT128_T)
		/* 16 byte reads from same location */
		(void)*(data128);
		stress_asm_mb();
#endif
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdints method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_bits(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		register uint8_t val8;

		val8 = *(data8);
		(void)val8;

		val8 = (uint8_t)(1U << (i & 7));
		*data8 = val8;
		stress_asm_mb();
		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: bits method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
		val8 ^= 0xff;
		*data8 = val8;
		stress_asm_mb();
		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: bits method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

#if defined(SHIM_ATOMIC_INC)
static int stress_cacheline_atomicinc(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + idx;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

PRAGMA_UNROLL
	for (i = 0; i < 1024; i++) {
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		SHIM_ATOMIC_INC(data8);
		val8 += 7;

		if (UNLIKELY(*data8 != val8)) {
			pr_fail("%s: atomicinc method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, idx, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
#endif

static int stress_cacheline_all(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size);

static const stress_cacheline_method_t cacheline_methods[] = {
	{ "all",	stress_cacheline_all },
	{ "adjacent",	stress_cacheline_adjacent },
#if defined (SHIM_ATOMIC_INC)
	{ "atomicinc",	stress_cacheline_atomicinc },
#endif
	{ "bits",	stress_cacheline_bits },
	{ "copy",	stress_cacheline_copy },
	{ "inc",	stress_cacheline_inc },
	{ "mix",	stress_cacheline_mix },
	{ "rdfwd64",	stress_cacheline_rdfwd64 },
	{ "rdints",	stress_cacheline_rdints },
	{ "rdrev64",	stress_cacheline_rdrev64 },
	{ "rdwr",	stress_cacheline_rdwr },
};

static const char *stress_cacheline_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(cacheline_methods)) ? cacheline_methods[i].name : NULL;
}

static int stress_cacheline_all(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size)
{
	size_t i;
	const size_t n = SIZEOF_ARRAY(cacheline_methods);

	for (i = 1; LIKELY(stress_continue(args) && (i < n)); i++) {
		int rc;

		rc = cacheline_methods[i].func(args, idx, parent, l1_cacheline_size);
		if (rc != EXIT_SUCCESS)
			return rc;
	}
	return EXIT_SUCCESS;
}

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
/*
 *  stress_cacheline_change_affinity()
 *	pin process to CPU based on clock time * 100, instance number
 *	and parent/child offset modulo number of CPUs
 */
static inline void stress_cacheline_change_affinity(
	stress_args_t *args,
	const uint32_t n_cpus,
	const uint32_t *cpus,
	bool parent)
{
	if (n_cpus > 0) {
		cpu_set_t mask;
		const double now = stress_time_now() * 100;
		const uint32_t cpu_idx = ((uint32_t)args->instance + (uint32_t)parent + (uint32_t)now) % n_cpus;
		const uint32_t cpu = cpus[cpu_idx];

		CPU_ZERO(&mask);
		CPU_SET((int)cpu, &mask);
		VOID_RET(int, sched_setaffinity(0, sizeof(mask), &mask));
	}
}
#endif

static int stress_cacheline_child(
	stress_args_t *args,
	const int idx,
	const bool parent,
	const size_t l1_cacheline_size,
	stress_cacheline_func func,
	const bool cacheline_affinity)
{
	int rc;
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
	uint32_t *cpus;
	const uint32_t n_cpus = stress_get_usable_cpus(&cpus, true);
#endif

	(void)cacheline_affinity;

	do {
		rc = func(args, idx, parent, l1_cacheline_size);
		if (parent)
			stress_bogo_inc(args);

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
		if (cacheline_affinity)
			stress_cacheline_change_affinity(args, n_cpus, cpus, parent);
#endif
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
	stress_free_usable_cpus(&cpus);
#endif
	return rc;
}

/*
 *  stress_cacheline_init()
 *	called once by stress-ng, so we can set idx to 0
 */
static void stress_cacheline_init(const uint32_t instances)
{
	(void)instances;

	g_shared->cacheline.index = 0;
	g_shared->cacheline.lock = stress_lock_create("cacheline");
}

/*
 *  stress_cacheline_deinit()
 *	called once by stress-ng, so we can set idx to 0
 */
static void stress_cacheline_deinit(void)
{
	if (g_shared->cacheline.lock) {
		stress_lock_destroy(g_shared->cacheline.lock);
		g_shared->cacheline.lock = NULL;
		g_shared->cacheline.index = 0;
	}
}

static int stress_cacheline_next_idx(void)
{
	int ret;

	if (stress_lock_acquire(g_shared->cacheline.lock) < 0)
		return -1;

	ret = g_shared->cacheline.index;
	g_shared->cacheline.index++;

	if (stress_lock_release(g_shared->cacheline.lock) < 0)
		return -1;

	return ret * 2;
}

/*
 *  stress_cacheline()
 *	exercise a cacheline by multiple processes
 */
static int stress_cacheline(stress_args_t *args)
{
	size_t l1_cacheline_size = (size_t)get_L1_line_size(args);
	int idx;
	int rc = EXIT_SUCCESS;
	size_t cacheline_method = 0;
	stress_cacheline_func func;
	bool cacheline_affinity = false;
	size_t n_pids, i;
	stress_pid_t *s_pids = NULL, *s_pids_head = NULL;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	if (!g_shared->cacheline.lock) {
		pr_inf("%s: failed to initialize cacheline lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	idx = stress_cacheline_next_idx();
	if (idx < 0) {
		pr_inf("%s: failed to get cacheline idx, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	if (l1_cacheline_size > args->instances) {
		n_pids = (l1_cacheline_size - args->instances) / args->instances;
		if (((n_pids * args->instances) + args->instances) < l1_cacheline_size)
			n_pids++;
	} else {
		n_pids = 0;
	}

	if (stress_instance_zero(args))
		pr_inf("%s: running %zu processes per stressor instance (%zu cacheline processes in total)\n",
			args->name, (n_pids + 1), (n_pids + 1) * args->instances);

	if (n_pids > 1) {
		s_pids = stress_sync_s_pids_mmap(n_pids);
		if (s_pids == MAP_FAILED) {
			pr_inf_skip("%s: failed to mmap %zu PIDs%s, skipping stressor\n",
				args->name, n_pids, stress_get_memfree_str());
			return EXIT_NO_RESOURCE;
		}
	}

	(void)stress_get_setting("cacheline-affinity", &cacheline_affinity);
	(void)stress_get_setting("cacheline-method", &cacheline_method);

	if (stress_instance_zero(args)) {
		pr_dbg("%s: using method '%s'\n", args->name, cacheline_methods[cacheline_method].name);
		pr_dbg("%s: L1 cache line size %zd bytes\n", args->name, l1_cacheline_size);
	}

	func = cacheline_methods[cacheline_method].func;

	if (s_pids) {
		for (i = 0; i < n_pids; i++) {
			int child_idx;

			stress_sync_start_init(&s_pids[i]);

			child_idx = stress_cacheline_next_idx();
			if (child_idx < 0) {
				pr_inf("%s: failed to get cacheline idx, skipping stressor\n", args->name);
				rc = EXIT_NO_RESOURCE;
				goto finish;
			}
again:
			s_pids[i].pid = fork();
			if (s_pids[i].pid < 0) {
				if (stress_redo_fork(args, errno))
					goto again;
				if (UNLIKELY(!stress_continue(args)))
					goto finish;
				pr_err("%s: fork failed, errno=%d: (%s)\n",
					args->name, errno, strerror(errno));
				goto finish;
			} else if (s_pids[i].pid == 0) {
				s_pids[i].pid = getpid();
	
				stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
				stress_sync_start_wait_s_pid(&s_pids[i]);
				stress_set_proc_state(args->name, STRESS_STATE_RUN);
				stress_parent_died_alarm();
				rc = stress_cacheline_child(args, child_idx, false, l1_cacheline_size, func, cacheline_affinity);
				_exit(rc);
			} else {
				stress_sync_start_s_pid_list_add(&s_pids_head, &s_pids[i]);
			}
		}
	}
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	if (s_pids)
		stress_sync_start_cont_list(s_pids_head);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	stress_cacheline_child(args, idx, true, l1_cacheline_size, func, cacheline_affinity);
finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	if (s_pids && (n_pids > 1)) {
		stress_kill_and_wait_many(args, s_pids, n_pids, SIGALRM, true);
		(void)stress_sync_s_pids_munmap(s_pids, n_pids);
	}

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_cacheline_affinity, "cacheline-affinity", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_cacheline_method,   "cacheline-method",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_cacheline_method },
	END_OPT,
};

const stressor_info_t stress_cacheline_info = {
	.stressor = stress_cacheline,
	.classifier = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.opts = opts,
	.init = stress_cacheline_init,
	.deinit = stress_cacheline_deinit,
	.help = help
};
