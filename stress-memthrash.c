/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"memthrash N",		"start N workers thrashing a 16MB memory buffer" },
	{ NULL,	"memthrash-ops N",	"stop after N memthrash bogo operations" },
	{ NULL,	"memthrash-method M",	"specify memthrash method M, default is all" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_LIB_PTHREAD)

#define MATRIX_SIZE_MAX_SHIFT	(14)
#define MATRIX_SIZE_MIN_SHIFT	(10)
#define MATRIX_SIZE		(1 << MATRIX_SIZE_MAX_SHIFT)
#define MEM_SIZE		(MATRIX_SIZE * MATRIX_SIZE)

typedef void (*memthrash_func_t)(const args_t *args, size_t mem_size);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	memthrash_func_t	func;	/* the method function */
} stress_memthrash_method_info_t;

static const stress_memthrash_method_info_t memthrash_methods[];
static void *mem;
static volatile bool thread_terminate;
static sigset_t set;

#if (((defined(__GNUC__) || defined(__clang__)) && defined(STRESS_X86)) || \
    (defined(__GNUC__) && NEED_GNUC(4,7,0) && defined(STRESS_ARM)))
#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define MEM_LOCK(ptr)	 __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
#else
#define MEM_LOCK(ptr)	asm volatile("lock addl %1,%0" : "+m" (*ptr) : "ir" (1));
#endif
#endif

static inline HOT OPTIMIZE3 void stress_memthrash_random_chunk(const size_t chunk_size, size_t mem_size)
{
	uint32_t i;
	const uint32_t max = mwc16();
	size_t chunks = mem_size / chunk_size;

	if (chunks < 1)
		chunks = 1;

	for (i = 0; !thread_terminate && (i < max); i++) {
		const size_t chunk = mwc32() % chunks;
		const size_t offset = chunk * chunk_size;
#if defined(__GNUC__)
		(void)__builtin_memset((void *)mem + offset, mwc8(), chunk_size);
#else
		(void)memset((void *)mem + offset, mwc8(), chunk_size);
#endif
	}
}

static void HOT OPTIMIZE3 stress_memthrash_random_chunkpage(const args_t *args, size_t mem_size)
{
	stress_memthrash_random_chunk(args->page_size, mem_size);
}

static void HOT OPTIMIZE3 stress_memthrash_random_chunk256(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(256, mem_size);
}

static void HOT OPTIMIZE3 stress_memthrash_random_chunk64(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(64, mem_size);
}

static void HOT OPTIMIZE3 stress_memthrash_random_chunk8(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(8, mem_size);
}

static void HOT OPTIMIZE3 stress_memthrash_random_chunk1(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(1, mem_size);
}

static void stress_memthrash_memset(const args_t *args, size_t mem_size)
{
	(void)args;

#if defined(__GNUC__)
	(void)__builtin_memset((void *)mem, mwc8(), mem_size);
#else
	(void)memset((void *)mem, mwc8(), mem_size);
#endif
}

static void HOT OPTIMIZE3 stress_memthrash_flip_mem(const args_t *args, size_t mem_size)
{
	(void)args;

	volatile uint64_t *ptr = (volatile uint64_t *)mem;
	const uint64_t *end = (uint64_t *)(mem + mem_size);

	while (LIKELY(ptr < end)) {
		*ptr = *ptr ^ ~0ULL;
		ptr++;
	}
}

static void HOT OPTIMIZE3 stress_memthrash_matrix(const args_t *args, size_t mem_size)
{
	(void)args;
	(void)mem_size;

	size_t i, j;
	volatile uint8_t *vmem = mem;

	for (i = 0; !thread_terminate && (i < MATRIX_SIZE); i+= ((mwc8() & 0xf) + 1)) {
		for (j = 0; j < MATRIX_SIZE; j+= 16) {
			size_t i1 = (i * MATRIX_SIZE) + j;
			size_t i2 = (j * MATRIX_SIZE) + i;
			uint8_t tmp;

			tmp = vmem[i1];
			vmem[i1] = vmem[i2];
			vmem[i2] = tmp;
		}
	}
}

static void HOT OPTIMIZE3 stress_memthrash_prefetch(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const uint32_t max = mwc16();

	(void)args;

	for (i = 0; !thread_terminate && (i < max); i++) {
		size_t offset = mwc32() % mem_size;
		uint8_t *const ptr = mem + offset;
		volatile uint8_t *const vptr = ptr;

		__builtin_prefetch(ptr, 1, 1);
		//(void)*vptr;
		*vptr = i & 0xff;
	}
}

static void HOT OPTIMIZE3 stress_memthrash_flush(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const uint32_t max = mwc16();

	(void)args;

	for (i = 0; !thread_terminate && (i < max); i++) {
		size_t offset = mwc32() % mem_size;
		uint8_t *const ptr = mem + offset;
		volatile uint8_t *const vptr = ptr;

		*vptr = i & 0xff;
		clflush(ptr);
	}
}

static void HOT OPTIMIZE3 stress_memthrash_mfence(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const uint32_t max = mwc16();

	(void)args;

	for (i = 0; !thread_terminate && (i < max); i++) {
		size_t offset = mwc32() % mem_size;
		volatile uint8_t *ptr = mem + offset;

		*ptr = i & 0xff;
		mfence();
	}
}

#if defined(MEM_LOCK)
static void HOT OPTIMIZE3 stress_memthrash_lock(const args_t *args, size_t mem_size)
{
	uint32_t i;

	(void)args;

	for (i = 0; !thread_terminate && (i < 64); i++) {
		size_t offset = mwc32() % mem_size;
		volatile uint8_t *ptr = mem + offset;

		MEM_LOCK(ptr);
	}
}
#endif

static void HOT OPTIMIZE3 stress_memthrash_spinread(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const size_t offset = mwc32() % mem_size;
	volatile uint32_t *ptr = (uint32_t *)(mem + offset);

	(void)args;

	for (i = 0; !thread_terminate && (i < 65536); i++) {
		(void)*ptr;
		(void)*ptr;
		(void)*ptr;
		(void)*ptr;

		(void)*ptr;
		(void)*ptr;
		(void)*ptr;
		(void)*ptr;
	}
}

static void HOT OPTIMIZE3 stress_memthrash_spinwrite(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const size_t offset = mwc32() % mem_size;
	volatile uint32_t *ptr = (uint32_t *)(mem + offset);

	(void)args;

	for (i = 0; !thread_terminate && (i < 65536); i++) {
		*ptr = i;
		*ptr = i;
		*ptr = i;
		*ptr = i;

		*ptr = i;
		*ptr = i;
		*ptr = i;
		*ptr = i;
	}
}


static void stress_memthrash_all(const args_t *args, size_t mem_size);
static void stress_memthrash_random(const args_t *args, size_t mem_size);

static const stress_memthrash_method_info_t memthrash_methods[] = {
	{ "all",	stress_memthrash_all },		/* MUST always be first! */

	{ "chunk1",	stress_memthrash_random_chunk1 },
	{ "chunk8",	stress_memthrash_random_chunk8 },
	{ "chunk64",	stress_memthrash_random_chunk64 },
	{ "chunk256",	stress_memthrash_random_chunk256 },
	{ "chunkpage",	stress_memthrash_random_chunkpage },
	{ "flip",	stress_memthrash_flip_mem },
	{ "flush",	stress_memthrash_flush },
#if defined(MEM_LOCK)
	{ "lock",	stress_memthrash_lock },
#endif
	{ "matrix",	stress_memthrash_matrix },
	{ "memset",	stress_memthrash_memset },
	{ "mfence",	stress_memthrash_mfence },
	{ "prefetch",	stress_memthrash_prefetch },
	{ "random",	stress_memthrash_random },
	{ "spinread",	stress_memthrash_spinread },
	{ "spinwrite",	stress_memthrash_spinwrite }
};

static void stress_memthrash_all(const args_t *args, size_t mem_size)
{
	static size_t i = 1;
	const double t = time_now();

	do {
		memthrash_methods[i].func(args, mem_size);
	} while (!thread_terminate && (time_now() - t < 0.01));

	i++;
	if (UNLIKELY(i >= SIZEOF_ARRAY(memthrash_methods)))
		i = 1;
}

static void stress_memthrash_random(const args_t *args, size_t mem_size)
{
	/* loop until we find a good candidate */
	for (;;) {
		size_t i = mwc8() % SIZEOF_ARRAY(memthrash_methods);
		const memthrash_func_t func = (memthrash_func_t)memthrash_methods[i].func;

		/* Don't run stress_memthrash_random/all to avoid recursion */
		if ((func != stress_memthrash_random) &&
		    (func != stress_memthrash_all)) {
			func(args, mem_size);
			return;
		}
	}
}

/*
 *  stress_set_memthrash_method()
 *	set the default memthresh method
 */
static int stress_set_memthrash_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(memthrash_methods); i++) {
		const stress_memthrash_method_info_t *info = &memthrash_methods[i];
		if (!strcmp(memthrash_methods[i].name, name)) {
			set_setting("memthrash-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "memthrash-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(memthrash_methods); i++) {
		(void)fprintf(stderr, " %s", memthrash_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_memthrash_func()
 *	pthread that exits immediately
 */
static void *stress_memthrash_func(void *arg)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	static void *nowt = NULL;
	const pthread_args_t *parg = (pthread_args_t *)arg;
	const args_t *args = parg->args;
	const memthrash_func_t func = (memthrash_func_t)parg->data;

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	/*
	 *  According to POSIX.1 a thread should have
	 *  a distinct alternative signal stack.
	 *  However, we block signals in this thread
	 *  so this is probably just totally unncessary.
	 */
	(void)memset(stack, 0, sizeof(stack));
	if (stress_sigaltstack(stack, SIGSTKSZ) < 0)
		goto die;

	while (!thread_terminate && keep_stressing()) {
		size_t j;

		for (j = MATRIX_SIZE_MIN_SHIFT; j <= MATRIX_SIZE_MAX_SHIFT &&
		     !thread_terminate && keep_stressing(); j++) {
			size_t mem_size = 1 << (2 * j);

			size_t i;
			for (i = 0; i < SIZEOF_ARRAY(memthrash_methods); i++)
				if (func == memthrash_methods[i].func)
					break;
			func(args, mem_size);
			inc_counter(args);
			shim_sched_yield();
		}
	}

	/* Wait parent up, all done! */
	(void)kill(args->pid, SIGALRM);
die:
	return &nowt;
}

static inline uint32_t stress_memthrash_max(
	const uint32_t instances,
	const uint32_t total_cpus)
{
	if ((instances >= total_cpus) || (instances == 0)) {
		return 1;
	} else {
		uint32_t max = total_cpus / instances;
		return ((total_cpus % instances) == 0) ? max : max + 1;
	}
}

static inline uint32_t stress_memthash_optimal(
	const uint32_t instances,
	const uint32_t total_cpus)
{
	uint32_t n = instances;

	while (n > 1) {
		if (total_cpus % n == 0)
			return n;
		n--;
	}
	return 1;
}

static inline char *plural(uint32_t n)
{
	return n > 1 ? "s" : "";
}


/*
 *  stress_memthrash()
 *	stress by creating pthreads
 */
static int stress_memthrash(const args_t *args)
{
	const stress_memthrash_method_info_t *memthrash_method = &memthrash_methods[0];
	const uint32_t total_cpus = stress_get_processors_configured();
	const uint32_t max_threads = stress_memthrash_max(args->num_instances, total_cpus);
	pthread_t pthreads[max_threads];
	int ret[max_threads];
	pthread_args_t pargs;
	memthrash_func_t func;
	pid_t pid;

	(void)get_setting("memthrash-method", &memthrash_method);
	func = memthrash_method->func;

	pr_dbg("%s: using method '%s'\n", args->name, memthrash_method->name);
	if (args->instance == 0) {
		pr_inf("%s: starting %" PRIu32 " thread%s on each of the %"
			PRIu32 " stressors on a %" PRIu32 " CPU system\n",
			args->name, max_threads, plural(max_threads),
			args->num_instances, total_cpus);
		if (max_threads * args->num_instances > total_cpus) {
			pr_inf("%s: this is not an optimal choice of stressors, "
				"try %" PRIu32 " instead\n",
			args->name,
			stress_memthash_optimal(args->num_instances, total_cpus));
		}
	}

	pargs.args = args;
	pargs.data = func;

	(void)memset(pthreads, 0, sizeof(pthreads));
	(void)memset(ret, 0, sizeof(ret));
	(void)sigfillset(&set);

again:
	if (!g_keep_stressing_flag)
		return EXIT_SUCCESS;
	pid = fork();
	if (pid < 0) {
		if ((errno == EAGAIN) || (errno == ENOMEM))
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, waitret;

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		waitret = shim_waitpid(pid, &status, 0);
		if (waitret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)shim_waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				goto again;
			}
		}
	} else if (pid == 0) {
		uint32_t i;

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_POPULATE)
		flags |= MAP_POPULATE;
#endif

mmap_retry:
		mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (mem == MAP_FAILED) {
#if defined(MAP_POPULATE)
			flags &= ~MAP_POPULATE;	/* Less aggressive, more OOMable */
#endif
			if (!g_keep_stressing_flag) {
				pr_dbg("%s: mmap failed: %d %s\n",
					args->name, errno, strerror(errno));
				return EXIT_NO_RESOURCE;
			}
			(void)shim_usleep(100000);
			if (!g_keep_stressing_flag)
				goto reap_mem;
			goto mmap_retry;
		}

		for (i = 0; i < max_threads; i++) {
			ret[i] = pthread_create(&pthreads[i], NULL,
				stress_memthrash_func, (void *)&pargs);
			if (ret[i]) {
				/* Just give up and go to next thread */
				if (ret[i] == EAGAIN)
					continue;
				/* Something really unexpected */
				pr_fail_errno("pthread create", ret[i]);
				goto reap;
			}
			if (!g_keep_stressing_flag)
				goto reap;
		}
		/* Wait for SIGALRM or SIGINT/SIGHUP etc */
		(void)pause();

reap:
		thread_terminate = true;
		for (i = 0; i < max_threads; i++) {
			if (!ret[i]) {
				ret[i] = pthread_join(pthreads[i], NULL);
				if (ret[i])
					pr_fail_errno("pthread join", ret[i]);
			}
		}
reap_mem:
		(void)munmap(mem, MEM_SIZE);
	}
	return EXIT_SUCCESS;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_memthrash_method,	stress_set_memthrash_method },
	{ 0,			NULL }
};

stressor_info_t stress_memthrash_info = {
	.stressor = stress_memthrash,
	.class = CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else

static int stress_set_memthrash_method(const char *name)
{
	(void)name;

	(void)pr_inf("warning: --memthrash-method not available on this system\n");
	return 0;
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_memthrash_method,	stress_set_memthrash_method },
	{ 0,			NULL }
};

stressor_info_t stress_memthrash_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
