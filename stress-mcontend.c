/*
 * Copyright (C) 2018-2021 Canonical, Ltd.
 * Copyright (C) 2021-2024 Colin Ian King.
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
#include "core-arch.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-pthread.h"

static const stress_help_t help[] = {
	{ NULL,	"mcontend N",	  "start N workers that produce memory contention" },
	{ NULL,	"mcontend-ops N", "stop memory contention workers after N bogo-ops" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_LIB_PTHREAD)

static sigset_t set;

#define MAX_READ_THREADS	(4)
#define MAX_MAPPINGS		(2)

/*
 *  page_write_sync()
 *	write a whole page of zeros to the backing file and
 *	ensure it is sync'd to disc for mmap'ing to avoid any
 *	bus errors on the mmap.
 */
static int page_write_sync(const int fd, const size_t page_size)
{
	char ALIGN64 buffer[256];
	size_t n = 0;

	(void)shim_memset(buffer, 0, sizeof(buffer));

	while (n < page_size) {
		ssize_t rc;

		rc = write(fd, buffer, sizeof(buffer));
		if (rc < (ssize_t)sizeof(buffer))
			return (int)rc;
		n += (size_t)rc;
	}
	(void)sync();

	return 0;
}

static inline OPTIMIZE3 void read64(uint64_t *data)
{
	register uint64_t v;
	const volatile uint64_t *vdata = data;

	shim_builtin_prefetch(data);
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

#if defined(HAVE_ASM_X86_LFENCE)
static inline OPTIMIZE3 void read64_lfence(uint64_t *data)
{
	register uint64_t v;
	const volatile uint64_t *vdata = data;

	v = vdata[0];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[1];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[3];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[4];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[5];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[6];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[7];
	(void)v;
	stress_asm_x86_lfence();
	v = vdata[8];
	(void)v;
	stress_asm_x86_lfence();
}
#else
static inline void read64_lfence(uint64_t *data)
{
	(void)data;
}
#endif

/*
 *  stress_memory_contend()
 *	read a proc file
 */
static inline OPTIMIZE3 void stress_memory_contend(const stress_pthread_args_t *pa)
{
	uint64_t **mappings = pa->data;
	volatile uint64_t *vdata0 = mappings[0];
	volatile uint64_t *vdata1 = mappings[1];
	uint64_t *data0 = mappings[0];
	uint64_t *data1 = mappings[1];
	register int i;

	for (i = 0; i < 1024; i++) {
		vdata0[0] = (uint64_t)i;
		vdata1[0] = (uint64_t)i;
		vdata0[1] = (uint64_t)i;
		vdata1[1] = (uint64_t)i;
		vdata0[2] = (uint64_t)i;
		vdata1[2] = (uint64_t)i;
		vdata0[3] = (uint64_t)i;
		vdata1[3] = (uint64_t)i;
		vdata0[4] = (uint64_t)i;
		vdata1[4] = (uint64_t)i;
		vdata0[5] = (uint64_t)i;
		vdata1[5] = (uint64_t)i;
		vdata0[6] = (uint64_t)i;
		vdata1[6] = (uint64_t)i;
		vdata0[7] = (uint64_t)i;
		vdata1[7] = (uint64_t)i;
		read64(data0);
		read64(data1);
		read64_lfence(data0);
		read64_lfence(data1);
	}

	for (i = 0; i < 1024; i++) {
		vdata0[0] = (uint64_t)i;
		shim_mfence();
		vdata1[0] = (uint64_t)i;
		shim_mfence();
		vdata0[1] = (uint64_t)i;
		shim_mfence();
		vdata1[1] = (uint64_t)i;
		shim_mfence();
		vdata0[2] = (uint64_t)i;
		shim_mfence();
		vdata1[2] = (uint64_t)i;
		shim_mfence();
		vdata0[3] = (uint64_t)i;
		shim_mfence();
		vdata1[3] = (uint64_t)i;
		shim_mfence();
		vdata0[4] = (uint64_t)i;
		shim_mfence();
		vdata1[4] = (uint64_t)i;
		shim_mfence();
		vdata0[5] = (uint64_t)i;
		shim_mfence();
		vdata1[5] = (uint64_t)i;
		shim_mfence();
		vdata0[6] = (uint64_t)i;
		shim_mfence();
		vdata1[6] = (uint64_t)i;
		shim_mfence();
		vdata0[7] = (uint64_t)i;
		shim_mfence();
		vdata1[7] = (uint64_t)i;
		shim_mfence();
		read64(data0);
		read64(data1);
	}

	for (i = 0; i < 1024; i++) {
		vdata0[0] = (uint64_t)i;
		vdata1[0] = (uint64_t)i;
		vdata0[1] = (uint64_t)i;
		vdata1[1] = (uint64_t)i;
		vdata0[2] = (uint64_t)i;
		vdata1[2] = (uint64_t)i;
		vdata0[3] = (uint64_t)i;
		vdata1[3] = (uint64_t)i;
		vdata0[4] = (uint64_t)i;
		vdata1[4] = (uint64_t)i;
		vdata0[5] = (uint64_t)i;
		vdata1[5] = (uint64_t)i;
		vdata0[6] = (uint64_t)i;
		vdata1[6] = (uint64_t)i;
		vdata0[7] = (uint64_t)i;
		vdata1[7] = (uint64_t)i;
		shim_clflush(data0);
		shim_clflush(data1);
		read64(data0);
		read64(data1);
	}

#if defined(STRESS_ARCH_X86) &&	\
    defined(HAVE_ASM_X86_PAUSE)
	for (i = 0; i < 1024; i++) {
		vdata0[0] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[0] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[1] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[1] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[2] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[2] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[3] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[3] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[4] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[4] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[5] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[5] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[6] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[6] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata0[7] = (uint64_t)i;
		stress_asm_x86_pause();
		vdata1[7] = (uint64_t)i;
		stress_asm_x86_pause();
		read64(data0);
		read64(data1);
	}
#endif
	for (i = 0; i < 1024; i++) {
		vdata0[0] = (uint64_t)i;
		stress_asm_mb();
		vdata1[0] = (uint64_t)i;
		stress_asm_mb();
		vdata0[1] = (uint64_t)i;
		stress_asm_mb();
		vdata1[1] = (uint64_t)i;
		stress_asm_mb();
		vdata0[2] = (uint64_t)i;
		stress_asm_mb();
		vdata1[2] = (uint64_t)i;
		stress_asm_mb();
		vdata0[3] = (uint64_t)i;
		stress_asm_mb();
		vdata1[3] = (uint64_t)i;
		stress_asm_mb();
		vdata0[4] = (uint64_t)i;
		stress_asm_mb();
		vdata1[4] = (uint64_t)i;
		stress_asm_mb();
		vdata0[5] = (uint64_t)i;
		stress_asm_mb();
		vdata1[5] = (uint64_t)i;
		stress_asm_mb();
		vdata0[6] = (uint64_t)i;
		stress_asm_mb();
		vdata1[6] = (uint64_t)i;
		stress_asm_mb();
		vdata0[7] = (uint64_t)i;
		stress_asm_mb();
		vdata1[7] = (uint64_t)i;
		stress_asm_mb();
		read64(data0);
		read64(data1);
	}
	(void)shim_cacheflush((void *)data0, 64, SHIM_DCACHE);
	(void)shim_cacheflush((void *)data1, 64, SHIM_DCACHE);
}

/*
 *  stress_memory_contend_thread
 */
static void *stress_memory_contend_thread(void *arg)
{
	static void *nowt = NULL;
	const stress_pthread_args_t *pa = (const stress_pthread_args_t *)arg;
#if defined(HAVE_SCHED_SETAFFINITY)
	const uint32_t cpus = (uint32_t)stress_get_processors_configured();
#endif

	/*
	 *  Block all signals, let controlling thread
	 *  handle these
	 */
	(void)sigprocmask(SIG_BLOCK, &set, NULL);

	while (stress_continue_flag()) {
#if defined(HAVE_SCHED_SETAFFINITY)
		cpu_set_t mask;
		const uint32_t cpu = stress_mwc32modn(cpus);
#endif
		stress_memory_contend(pa);

#if defined(HAVE_SCHED_SETAFFINITY)

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
static int stress_mcontend(stress_args_t *args)
{
	size_t i;
	pthread_t pthreads[MAX_READ_THREADS];
	int ret[MAX_READ_THREADS];
	void *data[MAX_MAPPINGS];
	char filename[PATH_MAX];
	stress_pthread_args_t pa;
	int fd, rc;

	rc = stress_temp_dir_mk_args(args);
	if (rc < 0)
		return stress_exit_status(-rc);
	(void)stress_temp_filename_args(args,
		filename, sizeof(filename), stress_mwc32());

	fd = open(filename, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		pr_inf("%s: open failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)shim_unlink(filename);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	(void)shim_unlink(filename);

	rc = page_write_sync(fd, args->page_size);
	if (rc < 0) {
		pr_inf("%s: mmap backing file write failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;

	}

	pa.args = args;
	/*
	 *  Get two different mappings of the same physical page
	 *  just to make things more interesting
	 */
	data[0] = stress_mmap_populate(NULL, args->page_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (data[0] == MAP_FAILED) {
		pr_inf("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	data[1] = stress_mmap_populate(NULL, args->page_size , PROT_READ | PROT_WRITE,
			MAP_PRIVATE, fd, 0);
	if (data[1] == MAP_FAILED) {
		pr_inf("%s: mmap failed: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		(void)munmap(data[0], args->page_size);
		(void)close(fd);
		(void)stress_temp_dir_rm_args(args);
		return EXIT_NO_RESOURCE;
	}
	(void)close(fd);
	(void)shim_mlock(data[0], args->page_size);
	(void)shim_mlock(data[1], args->page_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

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
		stress_bogo_inc(args);
	} while (stress_continue(args));


	for (i = 0; i < MAX_READ_THREADS; i++) {
		if (ret[i] == 0)
			(void)pthread_join(pthreads[i], NULL);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(data[0], args->page_size);
	(void)munmap(data[1], args->page_size);

	(void)stress_temp_dir_rm_args(args);

	return EXIT_SUCCESS;
}

stressor_info_t stress_mcontend_info = {
	.stressor = stress_mcontend,
	.class = CLASS_MEMORY,
	.help = help
};
#else
stressor_info_t stress_mcontend_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_MEMORY,
	.help = help,
	.unimplemented_reason = "built without pthread support"
};
#endif
