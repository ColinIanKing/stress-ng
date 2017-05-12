/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

#if defined(HAVE_LIB_PTHREAD)

#define MATRIX_SIZE_MAX_SHIFT	(12)
#define MATRIX_SIZE_MIN_SHIFT	(6)
#define MATRIX_SIZE		(1 << MATRIX_SIZE_MAX_SHIFT)
#define MEM_SIZE		(MATRIX_SIZE * MATRIX_SIZE)

typedef void (*thrash_func_t)(const args_t *args, size_t mem_size);

static volatile uint8_t *mem;
static volatile bool thread_terminate;
static sigset_t set;

#if (((defined(__GNUC__) || defined(__clang__)) && defined(STRESS_X86)) || \
    (defined(__GNUC__) && NEED_GNUC(4,7,0) && defined(STRESS_ARM))) && defined(__linux__)

#if defined(__GNUC__) && NEED_GNUC(4,7,0)
#define MEM_LOCK(ptr)	 __atomic_add_fetch(ptr, 1, __ATOMIC_SEQ_CST);
#else
#define MEM_LOCK(ptr)	asm volatile("lock addl %1,%0" : "+m" (*ptr) : "ir" (1));
#endif
#endif

static void stress_memthrash_random_chunk(const size_t chunk_size, size_t mem_size)
{
	uint32_t i;
	const size_t chunks = mem_size / chunk_size;

	for (i = 0; !thread_terminate && (i < mwc16()); i++) {
		const size_t chunk = mwc32() % chunks;
		const size_t offset = chunk * chunk_size;

		memset((void *)mem + offset, mwc8(), chunk_size);
	}
}

static void stress_memthrash_random_page(const args_t *args, size_t mem_size)
{
	stress_memthrash_random_chunk(args->page_size, mem_size);
}

static void stress_memthrash_random_chunk256(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(256, mem_size);
}

static void stress_memthrash_random_chunk64(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(64, mem_size);
}

static void stress_memthrash_random_chunk8(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(8, mem_size);
}

static void stress_memthrash_random_chunk1(const args_t *args, size_t mem_size)
{
	(void)args;

	stress_memthrash_random_chunk(1, mem_size);
}

static void stress_memthrash_all_mem(const args_t *args, size_t mem_size)
{
	(void)args;

	memset((void *)mem, mwc8(), mem_size);
}

static void stress_memthrash_flip_mem(const args_t *args, size_t mem_size)
{
	(void)args;

	uint64_t *ptr = (uint64_t *)mem;
	const uint64_t *end = (uint64_t *)(mem + mem_size);

	while (ptr < end) {
		*ptr = *ptr ^ ~0ULL;
		ptr++;
	}
}

static void stress_memthrash_matrix(const args_t *args, size_t mem_size)
{
	(void)args;
	(void)mem_size;

	size_t i, j;

	for (i = 0; !thread_terminate && (i < MATRIX_SIZE); i++) {
		for (j = 0; j < MATRIX_SIZE; j++) {
			size_t i1 = (i * MATRIX_SIZE) + j;
			size_t i2 = (j * MATRIX_SIZE) + i;
			uint8_t tmp;

			tmp = mem[i1];
			mem[i1] = mem[i2];
			mem[i2] = tmp;
		}
	}
}

static void stress_memthrash_prefetch(const args_t *args, size_t mem_size)
{
	uint32_t i;

	(void)args;

	for (i = 0; !thread_terminate && (i < mwc16()); i++) {
		size_t offset = mwc32() % mem_size;
		volatile uint8_t *ptr = mem + offset;

		__builtin_prefetch((void *)ptr, 1, 1);
		*ptr = i & 0xff;
	}
}

static void stress_memthrash_flush(const args_t *args, size_t mem_size)
{
	uint32_t i;

	(void)args;

	for (i = 0; !thread_terminate && (i < mwc16()); i++) {
		size_t offset = mwc32() % mem_size;
		volatile uint8_t *ptr = mem + offset;

		*ptr = i & 0xff;
		clflush((void *)ptr);
	}
}

static void stress_memthrash_mfence(const args_t *args, size_t mem_size)
{
	uint32_t i;

	(void)args;

	for (i = 0; !thread_terminate && (i < mwc16()); i++) {
		size_t offset = mwc32() % mem_size;
		volatile uint8_t *ptr = mem + offset;

		*ptr = i & 0xff;
		mfence();
	}
}

#if defined(MEM_LOCK)
static void stress_memthrash_lock(const args_t *args, size_t mem_size)
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

static void stress_memthrash_spinread(const args_t *args, size_t mem_size)
{
	uint32_t i;
	const size_t offset = mwc32() % mem_size;
	volatile uint32_t *ptr = (uint32_t *)(mem + offset);

	(void)args;

	for (i = 0; !thread_terminate && (i < 65536); i++) {
		*ptr;
		*ptr;
		*ptr;
		*ptr;

		*ptr;
		*ptr;
		*ptr;
		*ptr;
	}
}

static void stress_memthrash_spinwrite(const args_t *args, size_t mem_size)
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

static thrash_func_t thrash_funcs[] = {
	stress_memthrash_random_page,
	stress_memthrash_random_chunk256,
	stress_memthrash_random_chunk64,
	stress_memthrash_random_chunk8,
	stress_memthrash_random_chunk1,
	stress_memthrash_all_mem,
	stress_memthrash_flip_mem,
	stress_memthrash_matrix,
	stress_memthrash_prefetch,
	stress_memthrash_flush,
	stress_memthrash_mfence,
#if defined(MEM_LOCK)
	stress_memthrash_lock,
#endif
	stress_memthrash_spinread,
	stress_memthrash_spinwrite
};

/*
 *  stress_memthrash_func()
 *	pthread that exits immediately
 */
static void *stress_memthrash_func(void *parg)
{
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	static void *nowt = NULL;
	const args_t *args = ((pthread_args_t *)parg)->args;
	double t1;

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

	t1 = time_now();
	while (!thread_terminate) {
		size_t i = mwc8() % SIZEOF_ARRAY(thrash_funcs);

		do {
			size_t j;

			for (j = MATRIX_SIZE_MIN_SHIFT;
			     thread_terminate &&
			     (j <= MATRIX_SIZE_MAX_SHIFT); j++) {
				size_t mem_size = 1 << j;
				thrash_funcs[i](args, mem_size);
				inc_counter(args);
			}
		} while (!thread_terminate && (time_now() - t1 < 0.01));
	}
die:
	return &nowt;
}

static inline uint32_t stress_memthrash_max(const uint32_t instances)
{
	const uint32_t cpus = stress_get_processors_configured();

	if ((instances >= cpus) || (instances == 0)) {
		return 1;
	} else {
		uint32_t max = cpus / instances;
		return ((cpus % instances) == 0) ? max : max + 1;
	}
}


/*
 *  stress_memthrash()
 *	stress by creating pthreads
 */
int stress_memthrash(const args_t *args)
{
	const uint32_t max_threads = stress_memthrash_max(args->num_instances);
	uint32_t i;
	pthread_t pthreads[max_threads];
	int ret[max_threads];

	pthread_args_t pargs = { args };

	memset(pthreads, 0, sizeof(pthreads));
	memset(ret, 0, sizeof(ret));

	mem = mmap(NULL, MEM_SIZE, PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mem == MAP_FAILED) {
		pr_fail("mmap");
		return EXIT_NO_RESOURCE;
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


	(void)sigfillset(&set);

	/* Wait for SIGALRM or SIGINT/SIGHUP etc */
	pause();
	thread_terminate  = true;

reap:
	for (i = 0; i < max_threads; i++) {
		if (!ret[i]) {
			ret[i] = pthread_join(pthreads[i], NULL);
			if (ret[i])
				pr_fail_errno("pthread join", ret[i]);
		}
	}
	(void)munmap((void *)mem, MEM_SIZE);

	return EXIT_SUCCESS;
}
#else
int stress_memthrash(const args_t *args)
{
	return stress_not_implemented(args);
}
#endif
