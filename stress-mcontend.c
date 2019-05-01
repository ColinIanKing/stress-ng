/*
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
	{ NULL,	"mcontend N",	  "start N workers that produce memory contention" },
	{ NULL,	"mcontend-ops N", "stop memory contention workers after N bogo-ops" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_LIB_PTHREAD)

static sigset_t set;

#define MAX_READ_THREADS	(4)
#define MAX_MAPPINGS		(2)

static inline void mem_barrier(void)
{
	asm volatile("": : :"memory");
}

#if defined(STRESS_X86)
static inline void cpu_relax(void)
{
	asm volatile("pause\n": : :"memory");
}
#endif

static inline HOT OPTIMIZE3 void read64(uint64_t *data)
{
	register uint64_t v;
	volatile uint64_t *vdata = data;

	__builtin_prefetch(data);
	v = vdata[0];
	(void)v;
	v = vdata[1];
	(void)v;
	v = vdata[3];
	(void)v;
	v = vdata[4];
	(void)v;
	v = vdata[5];
	(void)v;
	v = vdata[6];
	(void)v;
	v = vdata[7];
	(void)v;
	v = vdata[8];
	(void)v;
}

/*
 *  stressmemory_contend()
 *	read a proc file
 */
static inline HOT OPTIMIZE3 void stress_memory_contend(const pthread_args_t *pa)
{
	uint64_t **mappings = pa->data;
	volatile uint64_t *vdata0 = mappings[0];
	volatile uint64_t *vdata1 = mappings[0];
	uint64_t *data0 = mappings[0];
	uint64_t *data1 = mappings[0];
	register int i;

	for (i = 0; i < 1024; i++) {
		vdata0[0] = i;
		vdata1[0] = i;
		vdata0[1] = i;
		vdata1[1] = i;
		vdata0[2] = i;
		vdata1[2] = i;
		vdata0[3] = i;
		vdata1[3] = i;
		vdata0[4] = i;
		vdata1[4] = i;
		vdata0[5] = i;
		vdata1[5] = i;
		vdata0[6] = i;
		vdata1[6] = i;
		vdata0[7] = i;
		vdata1[7] = i;
		read64(data0);
		read64(data1);
	}

	for (i = 0; i < 1024; i++) {
		vdata0[0] = i; mfence();
		vdata1[0] = i; mfence();
		vdata0[1] = i; mfence();
		vdata1[1] = i; mfence();
		vdata0[2] = i; mfence();
		vdata1[2] = i; mfence();
		vdata0[3] = i; mfence();
		vdata1[3] = i; mfence();
		vdata0[4] = i; mfence();
		vdata1[4] = i; mfence();
		vdata0[5] = i; mfence();
		vdata1[5] = i; mfence();
		vdata0[6] = i; mfence();
		vdata1[6] = i; mfence();
		vdata0[7] = i; mfence();
		vdata1[7] = i; mfence();
		read64(data0);
		read64(data1);
	}

#if defined(STRESS_X86)
	for (i = 0; i < 1024; i++) {
		vdata0[0] = i;
		vdata1[0] = i;
		vdata0[1] = i;
		vdata1[1] = i;
		vdata0[2] = i;
		vdata1[2] = i;
		vdata0[3] = i;
		vdata1[3] = i;
		vdata0[4] = i;
		vdata1[4] = i;
		vdata0[5] = i;
		vdata1[5] = i;
		vdata0[6] = i;
		vdata1[6] = i;
		vdata0[7] = i;
		vdata1[7] = i;
		clflush(data0);
		clflush(data1);
		read64(data0);
		read64(data1);
	}

	for (i = 0; i < 1024; i++) {
		vdata0[0] = i; cpu_relax();
		vdata1[0] = i; cpu_relax();
		vdata0[1] = i; cpu_relax();
		vdata1[1] = i; cpu_relax();
		vdata0[2] = i; cpu_relax();
		vdata1[2] = i; cpu_relax();
		vdata0[3] = i; cpu_relax();
		vdata1[3] = i; cpu_relax();
		vdata0[4] = i; cpu_relax();
		vdata1[4] = i; cpu_relax();
		vdata0[5] = i; cpu_relax();
		vdata1[5] = i; cpu_relax();
		vdata0[6] = i; cpu_relax();
		vdata1[6] = i; cpu_relax();
		vdata0[7] = i; cpu_relax();
		vdata1[7] = i; cpu_relax();
		read64(data0);
		read64(data1);
	}
#endif
	for (i = 0; i < 1024; i++) {
		vdata0[0] = i; mem_barrier();
		vdata1[0] = i; mem_barrier();
		vdata0[1] = i; mem_barrier();
		vdata1[1] = i; mem_barrier();
		vdata0[2] = i; mem_barrier();
		vdata1[2] = i; mem_barrier();
		vdata0[3] = i; mem_barrier();
		vdata1[3] = i; mem_barrier();
		vdata0[4] = i; mem_barrier();
		vdata1[4] = i; mem_barrier();
		vdata0[5] = i; mem_barrier();
		vdata1[5] = i; mem_barrier();
		vdata0[6] = i; mem_barrier();
		vdata1[6] = i; mem_barrier();
		vdata0[7] = i; mem_barrier();
		vdata1[7] = i; mem_barrier();
		read64(data0);
		read64(data1);
	}
	(void)shim_cacheflush((void *)data0, 64, DCACHE);
	(void)shim_cacheflush((void *)data1, 64, DCACHE);
}

/*
 *  stress_memory_contend_thread
 */
static void *stress_memory_contend_thread(void *arg)
{
	static void *nowt = NULL;
	uint8_t stack[SIGSTKSZ + STACK_ALIGNMENT];
	const pthread_args_t *pa = (const pthread_args_t *)arg;
#if defined(HAVE_AFFINITY)
	const uint32_t cpus = stress_get_processors_configured();
#endif

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
		return &nowt;

	while (g_keep_stressing_flag) {
#if defined(HAVE_AFFINITY)
		cpu_set_t mask;
#endif
		stress_memory_contend(pa);

#if defined(HAVE_AFFINITY)
		const uint32_t cpu = mwc32() % cpus;

		CPU_ZERO(&mask);
		CPU_SET(cpu, &mask);
		(void)sched_setaffinity(0, sizeof(mask), &mask);
#endif
	}
	return &nowt;
}

/*
 *  stress_mcontend
 *	memory contention stress
 */
static int stress_mcontend(const args_t *args)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];
	void *data[MAX_MAPPINGS];
	char filename[PATH_MAX];
	pthread_args_t pa;
	int fd, rc;

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return exit_status(-rc);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), mwc32());

	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf("%s: open failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	(void)shim_fallocate(fd, 0, 0, args->page_size);
	(void)unlink(filename);
	(void)stress_temp_dir_rm_args(args);

	pa.args = args;
	/*
	 *  Get two different mappings of the same physical page
	 *  just to make things more interesting
	 */
	data[0] = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (data[0] == MAP_FAILED) {
		pr_inf("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	data[1] = mmap(NULL, args->page_size , PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (data[1] == MAP_FAILED) {
		pr_inf("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap(data[1], args->page_size);
		(void)close(fd);
		return EXIT_NO_RESOURCE;
	}
	(void)close(fd);
	(void)shim_mlock(data[0], args->page_size);
	(void)shim_mlock(data[1], args->page_size);

	pa.data = data;
	for (i = 0; i < MAX_READ_THREADS; i++) {
		ret[i] = pthread_create(&pthreads[i], NULL,
				stress_memory_contend_thread, &pa);
	}

	do {
		stress_memory_contend(&pa);
#if defined(HAVE_MSYNC)
		(void)msync(data[0], args->page_size, MS_ASYNC);
		(void)msync(data[1], args->page_size, MS_ASYNC);
#endif
		inc_counter(args);
	} while (keep_stressing());

	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}
	(void)munmap(data[0], args->page_size);
	(void)munmap(data[1], args->page_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mcontend_info = {
	.stressor = stress_mcontend,
	.class = CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_mcontend_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_MEMORY,
	.help = help
};
#endif
