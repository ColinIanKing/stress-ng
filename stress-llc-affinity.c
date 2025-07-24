/*
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-asm-x86.h"
#include "core-capabilities.h"
#include "core-cpu.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-target-clones.h"

#include <sched.h>

#define MIN_LLC_SIZE	(4 * KB)
#define MAX_LLC_SIZE	(4 * GB)

static const stress_help_t help[] = {
	{ NULL,	"llc-affinity N",	"start N workers exercising low level cache over all CPUs" },
	{ NULL,	"llc-affinity-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"llc-affinity-numa",	"bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"llc-affinity-ops N",	"stop after N low-level-cache bogo operations" },
	{ NULL,	NULL,			NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_llc_affinity_clflush, "llc-affinity-clflush", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_llc_affinity_mlock,   "llc-affinity-mlock",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_llc_affinity_numa,    "llc-affinity-numa",    TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_llc_affinity_size,    "llc-affinity-size",    TYPE_ID_SIZE_T_BYTES_VM, MIN_LLC_SIZE, MAX_LLC_SIZE, NULL },
	END_OPT,
};

#if defined(HAVE_SCHED_SETAFFINITY)

typedef void (*cache_line_func_t)(
        uint64_t *buf,
        const uint64_t *buf_end,
        double *duration,
        const size_t cache_line_size);

static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64(
	uint64_t *buf,
	const uint64_t *buf_end,
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

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBST)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64_ppc64_dcbst(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_ppc64_dcbst((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64_ppc_dcbst(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_ppc_dcbst((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64_x86_clfsh(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_x86_clflush((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_64_x86_clfshopt(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_x86_clflushopt((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n(
	uint64_t *buf,
	const uint64_t *buf_end,
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

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBST)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n_ppc64_dcbst(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_ppc64_dcbst((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n_ppc_dcbst(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_ppc_dcbst((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n_x86_clfsh(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_x86_clflush((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
static void TARGET_CLONES OPTIMIZE3 stress_llc_write_cache_line_n_x86_clfshopt(
	uint64_t *buf,
	const uint64_t *buf_end,
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
		stress_asm_x86_clflushopt((void *)ptr);
	}
	t2 = stress_time_now();

	*duration += (t2 - t1);
}
#endif

static void TARGET_CLONES OPTIMIZE3 stress_llc_read_cache_line_64(
	uint64_t *buf,
	const uint64_t *buf_end,
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
	const uint64_t *buf_end,
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
static int stress_llc_affinity(stress_args_t *args)
{
	uint32_t *cpus;
	const uint32_t n_cpus = stress_get_usable_cpus(&cpus, true);
	const size_t page_size = args->page_size;
	uint32_t cpu_idx = args->instance;
	size_t llc_affinity_size = 0, cache_line_size = 64, mmap_sz;
	uint64_t *buf, *buf_end;
	uint64_t affinity_changes = 0;
	double write_duration, read_duration, rate, writes, reads, t_start, duration;
	cache_line_func_t write_func, read_func;
	bool llc_affinity_mlock = false;
	bool llc_affinity_clflush = false;
	bool llc_affinity_numa = false;
	char *clflush_op = NULL;
	const int n_numa_nodes = stress_numa_nodes();
#if defined(HAVE_LINUX_MEMPOLICY_H)
        stress_numa_mask_t *numa_mask = NULL;
        stress_numa_mask_t *numa_nodes = NULL;
#endif

	stress_catch_sigill();

	(void)stress_get_setting("llc-affinity-clflush", &llc_affinity_clflush);
	(void)stress_get_setting("llc-affinity-mlock", &llc_affinity_mlock);
	(void)stress_get_setting("llc-affinity-numa", &llc_affinity_numa);
	(void)stress_get_setting("llc-affinity-size", &llc_affinity_size);

	if (llc_affinity_size == 0) {
		stress_cpu_cache_get_llc_size(&llc_affinity_size, &cache_line_size);
		if (llc_affinity_size == 0) {
			pr_inf_skip("%s: cannot determine cache details, skipping stressor\n",
				args->name);
			pr_inf("%s: alternatively use --llc-affinity-size to specify LLC cache size\n",
				args->name);
			stress_free_usable_cpus(&cpus);
			return EXIT_NO_RESOURCE;
		}
	}
        llc_affinity_size *= n_numa_nodes;
        if (!args->instance) {
		if (n_numa_nodes > 1)  {
			pr_inf("%s: scaling lower level cache size by number of numa nodes %d to %zdK\n",
				args->name, n_numa_nodes, llc_affinity_size / 1024);
		} else  {
			pr_inf("%s: using lower level cache size of %zuK\n",
				args->name, llc_affinity_size / 1024);
		}
	}

	mmap_sz = STRESS_MAXIMUM(n_cpus * page_size, llc_affinity_size);

	/*
	 *  Allocate a LLC sized buffer to exercise
	 */
	buf = (uint64_t *)stress_mmap_populate(NULL, mmap_sz, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (buf == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap region of %zu bytes%s, errno=%d (%s), "
			"skipping stressor\n", args->name, mmap_sz,
			stress_get_memfree_str(), errno, strerror(errno));
		stress_free_usable_cpus(&cpus);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(buf, mmap_sz, "llc-buffer");

	if (llc_affinity_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &numa_nodes, &numa_mask, "--llc-affinity-numa", &llc_affinity_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --llc-affinity-numa selected but not supported by this system, disabling option\n",
				args->name);
		llc_affinity_numa = false;
#endif
	}
#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (llc_affinity_numa && numa_mask && numa_nodes) {
		if (stress_numa_mask_nodes_get(numa_nodes) > 0)
			stress_numa_randomize_pages(args, numa_nodes, numa_mask, buf, mmap_sz, page_size);
		stress_numa_mask_free(numa_mask);
		stress_numa_mask_free(numa_nodes);
	}
#endif
	if (llc_affinity_mlock)
		(void)shim_mlock(buf, mmap_sz);
	buf_end = (uint64_t *)((uintptr_t)buf + mmap_sz);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	writes = 0.0;
	write_duration = 0.0;
	reads = 0.0;
	read_duration = 0.0;

	if (cache_line_size == 64) {
#if defined(STRESS_ARCH_PPC64) && 	\
    defined(HAVE_ASM_PPC64_DCBST)
		if (llc_affinity_clflush) {
			write_func = stress_llc_write_cache_line_64_ppc64_dcbst;
			clflush_op = "dcbst";
		} else
#endif
#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
		if (llc_affinity_clflush) {
			write_func = stress_llc_write_cache_line_64_ppc_dcbst;
			clflush_op = "dcbst";
		} else
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
		if (llc_affinity_clflush && stress_cpu_x86_has_clflushopt()) {
			write_func = stress_llc_write_cache_line_64_x86_clfshopt;
			clflush_op = "clflshopt";
		} else
#endif
#if defined(HAVE_ASM_X86_CLFLUSH)
		if (llc_affinity_clflush && stress_cpu_x86_has_clfsh()) {
			write_func = stress_llc_write_cache_line_64_x86_clfsh;
			clflush_op = "clflsh";
		} else
#endif
		write_func = stress_llc_write_cache_line_64;
		read_func = stress_llc_read_cache_line_64;
	} else {
#if defined(STRESS_ARCH_PPC64) && 	\
    defined(HAVE_ASM_PPC64_DCBST)
		if (llc_affinity_clflush) {
			write_func = stress_llc_write_cache_line_n_ppc64_dcbst;
			clflush_op = "dcbst";
		} else
#endif
#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
		if (llc_affinity_clflush) {
			write_func = stress_llc_write_cache_line_n_ppc_dcbst;
			clflush_op = "dcbst";
		} else
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
		if (llc_affinity_clflush && stress_cpu_x86_has_clflushopt()) {
			write_func = stress_llc_write_cache_line_n_x86_clfshopt;
			clflush_op = "clflshopt";
		} else
#endif
#if defined(HAVE_ASM_X86_CLFLUSH)
		if (llc_affinity_clflush && stress_cpu_x86_has_clfsh()) {
			write_func = stress_llc_write_cache_line_n_x86_clfsh;
			clflush_op = "clflsh";
		} else
#endif
		write_func = stress_llc_write_cache_line_n;
		read_func = stress_llc_read_cache_line_n;
	}

	if (clflush_op && (stress_instance_zero(args)))
		pr_inf("%s: using %s cache flushing op-code\n", args->name, clflush_op);

	t_start = stress_time_now();
	do {
		if (n_cpus > 0) {
			cpu_set_t mask;

			CPU_ZERO(&mask);
			CPU_SET((int)cpus[cpu_idx], &mask);

			cpu_idx++;
			cpu_idx = (cpu_idx >= n_cpus) ? 0 : cpu_idx;

			if (sched_setaffinity(0, sizeof(mask), &mask) == 0)
				affinity_changes++;
		}

		read_func(buf, buf_end, &read_duration, cache_line_size);
		reads += (double)mmap_sz;

		write_func(buf, buf_end, &write_duration, cache_line_size);
		writes += (double)mmap_sz;
		stress_bogo_inc(args);
	} while (stress_continue(args));

	duration = stress_time_now() - t_start;

	writes /= (double)MB;
	rate = write_duration > 0.0 ? (double)writes / write_duration : 0.0;
	stress_metrics_set(args, 0, "MB pec sec memory write rate",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	reads /= (double)MB;
	rate = read_duration > 0.0 ? (double)reads / read_duration : 0.0;
	stress_metrics_set(args, 1, "MB per sec memory read rate",
		rate, STRESS_METRIC_HARMONIC_MEAN);
	rate = duration > 0.0 ? (double)affinity_changes / duration : 0.0;
	stress_metrics_set(args, 2, "CPU affinity changes per sec",
		rate, STRESS_METRIC_HARMONIC_MEAN);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)buf, mmap_sz);

	stress_free_usable_cpus(&cpus);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_llc_affinity_info = {
	.stressor = stress_llc_affinity,
	.classifier = CLASS_CPU_CACHE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

const stressor_info_t stress_llc_affinity_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without sched_setaffinity() support"
};

#endif
