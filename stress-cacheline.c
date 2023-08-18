// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2022      Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"

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
        const stress_args_t *args,
        const int index,
        const bool parent,
        const size_t l1_cacheline_size);

typedef struct {
	const char *name;
	const stress_cacheline_func	func;
} stress_cacheline_method_t;

static uint64_t get_L1_line_size(const stress_args_t *args)
{
	uint64_t cache_size = DEFAULT_L1_SIZE;
#if defined(__linux__) ||	\
    defined(__APPLE__)
	stress_cpu_cache_cpus_t *cpu_caches;
	stress_cpu_cache_t *cache = NULL;

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
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	register uint8_t val8 = *(data8);
	volatile uint8_t *data8adjacent = (volatile uint8_t *)(((uintptr_t)data8) ^ 1);

	(void)parent;
	(void)l1_cacheline_size;

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
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_copy(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	const volatile uint8_t *data8adjacent = (volatile uint8_t *)(((uintptr_t)data8) ^ 1);

	(void)parent;
	(void)l1_cacheline_size;

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
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_inc(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

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
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdwr(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

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
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_mix(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)(uintptr_t)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	static uint8_t tmp = 0xa5;

	(void)parent;
	(void)l1_cacheline_size;

	for (i = 0; i < 1024; i++) {
		register uint8_t val8;

		*(data8) = tmp;
		EXERCISE((*data8));
		val8 = tmp;
		EXERCISE(val8);
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: mix method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
		tmp = val8;
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdrev64(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
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
		for (j = cacheline_size - 8; j >= 0; j -= 8) {
			volatile uint64_t *data64 = (volatile uint64_t *)(aligned_cacheline + (size_t)j);

			(void)*data64;
			stress_asm_mb();
		}
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdrev64 method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdfwd64(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
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
		for (j = 0; j < cacheline_size; j += 8) {
			volatile uint64_t *data64 = (volatile uint64_t *)(aligned_cacheline + j);

			(void)*data64;
			stress_asm_mb();
		}
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdfwd64: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_rdints(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	volatile uint16_t *data16 = (uint16_t *)(((uintptr_t)data8) & ~(uintptr_t)1);
	volatile uint32_t *data32 = (uint32_t *)(((uintptr_t)data8) & ~(uintptr_t)3);
	volatile uint64_t *data64 = (uint64_t *)(((uintptr_t)data8) & ~(uintptr_t)7);
#if defined(HAVE_INT128_T)
        volatile __uint128_t *data128 = (__uint128_t *)(((uintptr_t)data8) & ~(uintptr_t)15);
#endif

	(void)parent;
	(void)l1_cacheline_size;

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
		/* 116 byte reads from same location */
		(void)*(data128);
		stress_asm_mb();
#endif
		if (UNLIKELY(val8 != *data8)) {
			pr_fail("%s: rdints method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

static int stress_cacheline_bits(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;

	(void)parent;
	(void)l1_cacheline_size;

	for (i = 0; i < 1024; i++) {
		register uint8_t val8;

		val8 = *(data8);
		(void)val8;

		val8 = (uint8_t)(1U << (i & 7));
		*data8 = val8;
		stress_asm_mb();
		if (*data8 != val8) {
			pr_fail("%s: bits method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
		val8 ^= 0xff;
		*data8 = val8;
		stress_asm_mb();
		if (*data8 != val8) {
			pr_fail("%s: bits method: cache line error in offset 0x%x, expected %2" PRIx8 ", got %2" PRIx8 "\n",
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}

#if defined(SHIM_ATOMIC_INC)
static int stress_cacheline_atomicinc(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	register int i;
	volatile uint8_t *buffer = (volatile uint8_t *)g_shared->cacheline.buffer;
	volatile uint8_t *data8 = buffer + index;
	register uint8_t val8 = *(data8);

	(void)parent;
	(void)l1_cacheline_size;

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
				args->name, index, val8, *data8);
			return EXIT_FAILURE;
		}
	}
	return EXIT_SUCCESS;
}
#endif

static int stress_cacheline_all(
	const stress_args_t *args,
	const int index,
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

static int stress_cacheline_all(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size)
{
	size_t i;
	const size_t n = SIZEOF_ARRAY(cacheline_methods);

	for (i = 1; stress_continue(args) && (i < n); i++) {
		int rc;

		rc = cacheline_methods[i].func(args, index, parent, l1_cacheline_size);
		if (rc != EXIT_SUCCESS)
			return rc;
	}
	return EXIT_SUCCESS;
}

static int stress_set_cacheline_affinity(const char *opt)
{
	return stress_set_setting_true("cacheline-affinity", opt);
}

/*
 *  stress_set_cacheline_method()
 *	set the default cacheline stress method
 */
static int stress_set_cacheline_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(cacheline_methods); i++) {
		if (!strcmp(cacheline_methods[i].name, name)) {
			stress_set_setting("cacheline-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "cacheline-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(cacheline_methods); i++) {
		(void)fprintf(stderr, " %s", cacheline_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
/*
 *  stress_cacheline_change_affinity()
 *	pin process to CPU based on clock time * 100, instance number
 *	and parent/child offset modulo number of CPUs
 */
static inline void stress_cacheline_change_affinity(
	const stress_args_t *args,
	const uint32_t cpus,
	bool parent)
{
	cpu_set_t mask;
	double now = stress_time_now() * 100;
	uint32_t cpu = ((uint32_t)args->instance + (uint32_t)parent + (uint32_t)now) % cpus;

	CPU_ZERO(&mask);
	CPU_SET((int)cpu, &mask);
	VOID_RET(int, sched_setaffinity(0, sizeof(mask), &mask));
}
#endif

static int stress_cacheline_child(
	const stress_args_t *args,
	const int index,
	const bool parent,
	const size_t l1_cacheline_size,
	stress_cacheline_func func,
	const bool cacheline_affinity)
{
	int rc;
#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
#endif

	(void)cacheline_affinity;

	do {
		rc = func(args, index, parent, l1_cacheline_size);
		if (parent)
			stress_bogo_inc(args);

#if defined(HAVE_SCHED_GETAFFINITY) &&	\
    defined(HAVE_SCHED_SETAFFINITY)
		if (cacheline_affinity)
			stress_cacheline_change_affinity(args, cpus, parent);
#endif
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	return rc;
}

/*
 *  stress_cacheline_init()
 *	called once by stress-ng, so we can set index to 0
 */
static void stress_cacheline_init(void)
{
	g_shared->cacheline.index = 0;
	g_shared->cacheline.lock = stress_lock_create();
}

/*
 *  stress_cacheline_deinit()
 *	called once by stress-ng, so we can set index to 0
 */
static void stress_cacheline_deinit(void)
{
	if (g_shared->cacheline.lock) {
		stress_lock_destroy(g_shared->cacheline.lock);
		g_shared->cacheline.lock = NULL;
		g_shared->cacheline.index = 0;
	}
}

static int stress_cacheline_next_index(void)
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
 *	execise a cacheline by multiple processes
 */
static int stress_cacheline(const stress_args_t *args)
{
	size_t l1_cacheline_size = (size_t)get_L1_line_size(args);
	int index;
	pid_t pid;
	int rc = EXIT_SUCCESS;
	size_t cacheline_method = 0;
	stress_cacheline_func func;
	bool cacheline_affinity = false;

	if (stress_sigchld_set_handler(args) < 0)
		return EXIT_NO_RESOURCE;

	if (!g_shared->cacheline.lock) {
		pr_inf("%s: failed to initialized cacheline lock, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	index = stress_cacheline_next_index();
	if (index < 0) {
		pr_inf("%s: failed to get cacheline index, skipping stressor\n", args->name);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("cacheline-affinity", &cacheline_affinity);
	(void)stress_get_setting("cacheline-method", &cacheline_method);

	if (args->instance == 0) {
		pr_dbg("%s: using method '%s'\n", args->name, cacheline_methods[cacheline_method].name);
		pr_dbg("%s: L1 cache line size %zd bytes\n", args->name, l1_cacheline_size);

		if ((args->num_instances * 2) < l1_cacheline_size) {
			pr_inf("%s: to fully exercise a %zd byte cache line, %zd instances are required\n",
				args->name, l1_cacheline_size, l1_cacheline_size / 2);
		}
	}

	func = cacheline_methods[cacheline_method].func;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
again:
	pid = fork();
	if (pid < 0) {
		if (stress_redo_fork(args, errno))
			goto again;
		if (!stress_continue(args))
			goto finish;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	} else if (pid == 0) {
		rc = stress_cacheline_child(args, index + 1, false, l1_cacheline_size, func, cacheline_affinity);
		_exit(rc);
	} else {
		stress_cacheline_child(args, index, true, l1_cacheline_size, func, cacheline_affinity);
		stress_kill_and_wait(args, pid, SIGALRM, false);
	}

finish:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_cacheline_affinity,	stress_set_cacheline_affinity },
	{ OPT_cacheline_method,		stress_set_cacheline_method },
	{ 0,				NULL },
};

stressor_info_t stress_cacheline_info = {
	.stressor = stress_cacheline,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_ALWAYS,
	.opt_set_funcs = opt_set_funcs,
	.init = stress_cacheline_init,
	.deinit = stress_cacheline_deinit,
	.help = help
};
