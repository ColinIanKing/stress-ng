/*
 * Copyright (C) 2026      Colin Ian King.
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
#include "core-interrupts.h"
#include "core-killpid.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-pthread.h"

#include <ctype.h>
#include <sched.h>

#if defined(HAVE_LINUX_MEMPOLICY_H)
#include <linux/mempolicy.h>
#endif

#define MIN_TLB_NUMA_ENTRIES		(2)
#define MAX_TLB_NUMA_ENTRIES		(1024 * 1024)
#define DEFAULT_TLB_NUMA_ENTRIES	(512)

static const stress_help_t help[] = {
	{ NULL,	"tlb-numa N",         "start N workers that force TLB shootdowns on NUMA systems" },
	{ NULL, "tlb-numa-entires N", "select number of TLB page entries per instance to use (default 512)" },
	{ NULL,	"tlb-nuna-nombind",   "disable setting NUMA policy on pages" },
	{ NULL,	"tlb-nuna-nopageout", "disable paging out randomly selected pages" },
	{ NULL,	"tlb-numa-ops N",     "stop after N TLB shootdown bogo ops" },
	{ NULL,	NULL,                 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_tlb_numa_entries,   "tlb-numa-entries",   TYPE_ID_UINT32, MIN_TLB_NUMA_ENTRIES, MAX_TLB_NUMA_ENTRIES, NULL },
	{ OPT_tlb_numa_nombind,   "tlb-numa-nombind",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_tlb_numa_nopageout, "tlb-numa-nopageout", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

#if defined(HAVE_SCHED_SETAFFINITY) && 	\
    defined(__NR_mbind)

typedef void * (*stress_tlb_pthread_t)(void *parg);

typedef struct {
	size_t page_size;			/* size of a page */
	size_t mmap_size;			/* size of mmaping */
	size_t mmap_pages;			/* number of page mappings */
	uint8_t **pages1;			/* pages array #1 */
	uint8_t **pages2;			/* pages array #2 */
	void *lock;				/* bogo op lock */
	stress_args_t *args;			/* args */
	uint32_t cpus;				/* number of CPUs */
	stress_numa_mask_t *numa_mask0;		/* NUMA mask for main instance */
	stress_numa_mask_t *numa_mask1;		/* NUMA mask for pthread 1 */
	stress_numa_mask_t *numa_mask2;		/* NUMA mask for pthread 2 */
	stress_numa_mask_t *numa_mask_ff;	/* NUMA mask, all bits set */
	stress_numa_mask_t *numa_nodes;		/* NUMA nodes available */
	bool nombind;				/* disable NUMA mem policy */
	bool nopageout;				/* disable paging-out pages */
} stress_tlb_numa_t;

typedef struct {
	pthread_t	pthread;		/* pthread */
	int		ret;			/* pthread create return */
} stress_tlb_numa_pthread_t;

/*
 *  stress_tlb_numa_shuffle_pages()
 *	randomly shuffle page mapping order
 */
static void stress_tlb_numa_shuffle_pages(
	uint8_t **pages,
	const size_t n_pages)
{
	register size_t i;

	for (i = 0; i < n_pages; i++) {
		register uint8_t *tmp;
		register const size_t j = stress_mwcsizemodn(n_pages);

		tmp = pages[i];
		pages[i] = pages[j];
		pages[j] = tmp;
	}
}

/*
 *  stress_tlb_numa_change_cpu()
 *	randomly change CPU affinity
 */
static void stress_tlb_numa_change_cpu(const uint32_t cpus, uint32_t *prev_cpu)
{
	cpu_set_t mask;
	uint32_t cpu;

	/*
	 *  find random cpu that's not the same as previous
	 *  only if we have more than 1 cpu
	 */
	do {
		cpu = stress_mwc32modn(cpus);
	} while ((cpus > 1) && (cpu == *prev_cpu));

	*prev_cpu = cpu;

	CPU_ZERO(&mask);
	CPU_SET(cpu, &mask);
	(void)sched_setaffinity(0, sizeof(mask), &mask);
	shim_sched_yield();
}

/*
 *  stress_tlb_numa_shootdown_mmap()
 *	mmap with retries
 */
static void *stress_tlb_numa_mmap(
	stress_args_t *args,
	void *addr,
	size_t length,
	int prot,
	int flags,
	int fd,
	off_t offset)
{
	int retry = 128;
	void *mem;

	do {
		mem = mmap(addr, length, prot, flags, fd, offset);
		if (LIKELY((void *)mem != MAP_FAILED)) {
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_NOHUGEPAGE)
			(void)shim_madvise(mem, length, SHIM_MADV_NOHUGEPAGE);
#endif
			stress_memory_anon_name_set(mem, length, "tlb-shootdown-buffer");
			return mem;
		}
		if ((errno == EAGAIN) ||
		    (errno == ENOMEM) ||
		    (errno == ENFILE)) {
			retry--;
		} else {
			break;
		}
	} while (retry > 0);

	pr_inf_skip("%s: failed to mmap %zu bytes%s, errno=%d (%s), skipping stressor\n",
		args->name, length, stress_memory_free_get(),
		errno, strerror(errno));
	return mem;
}

/*
 *  stress_tlb_numa_mbind_do()
 *	1 in 16 return true
 */
static inline bool stress_tlb_numa_mbind_do(void)
{
	return stress_mwc8() > 240;
}

/*
 *  stress_tlb_numa_mbind()
 *	mbind pages to a NUMA node
 */
static inline void OPTIMIZE3 stress_tlb_numa_mbind(
	stress_tlb_numa_t *tlb_numa,
	const long int node,
	stress_numa_mask_t *numa_mask,
	uint8_t **pages)
{
	size_t i;

	shim_memset(numa_mask->mask, 0x00, numa_mask->mask_size);
	STRESS_SETBIT(numa_mask->mask, node);

	for (i = 0; i < tlb_numa->mmap_pages; i++) {
		if (stress_tlb_numa_mbind_do()) {
			(void)shim_mbind((void *)pages[i], tlb_numa->page_size, MPOL_BIND, numa_mask->mask,
					numa_mask->max_nodes, MPOL_MF_STRICT);
		}
	}
}

/*
 *  stress_tlb_pthread1()
 *	pthread 1, work on mapping 1 pages
 */
static void OPTIMIZE3 *stress_tlb_pthread1(void *parg)
{
	stress_tlb_numa_t *tlb_numa = (stress_tlb_numa_t *)parg;
	long int node = 0;
	const size_t page_size = tlb_numa->page_size;
	uint32_t prev_cpu = 0;

	do {
		uint8_t *addr;
		const size_t size = page_size * (2 + (stress_mwc8() & 0xf));

		addr = (uint8_t *)stress_mmap_populate(NULL, size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (addr != MAP_FAILED) {
			*(addr + 0) = 0x5a;
			*(addr + page_size) = 0xa5;
		}

		stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);
		if (!tlb_numa->nombind) {
			node = stress_numa_next_node(node, tlb_numa->numa_nodes);
			if (node < 0)
				node = 0;
			stress_tlb_numa_mbind(tlb_numa, node, tlb_numa->numa_mask1, tlb_numa->pages2);
		}

		if (addr != MAP_FAILED) {
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_PAGEOUT)
			if (!tlb_numa->nopageout)
				(void)shim_madvise((void *)addr, size, SHIM_MADV_PAGEOUT);
#endif
			(void)munmap((void *)addr, size);
		}

		if (!tlb_numa->nombind) {
			stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);
			node = stress_numa_next_node(node, tlb_numa->numa_nodes);
			if (node < 0)
				node = 0;
			stress_tlb_numa_mbind(tlb_numa, node, tlb_numa->numa_mask1, tlb_numa->pages1);
		}
	} while (stress_bogo_inc_lock(tlb_numa->args, tlb_numa->lock, true));

	return &g_nowt;
}

/*
 *  stress_tlb_pthread2()
 *	pthread 2, work on mapping 2 pages
 */
static void OPTIMIZE3 *stress_tlb_pthread2(void *parg)
{
	stress_tlb_numa_t *tlb_numa = (stress_tlb_numa_t *)parg;
	long int node = 0;
	const size_t page_size = tlb_numa->page_size;
	uint32_t prev_cpu = 0;

	do {
		uint8_t *addr;
		const size_t size = page_size * (2 + (stress_mwc8() & 0xf));

		addr = (uint8_t *)stress_mmap_populate(NULL, size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (addr != MAP_FAILED) {
			*(addr + 0) = 0x5a;
			*(addr + page_size) = 0xa5;
		}

		if (!tlb_numa->nombind) {
			stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);
			node = stress_numa_next_node(node, tlb_numa->numa_nodes);
			if (node < 0)
				node = 0;
			stress_tlb_numa_mbind(tlb_numa, node, tlb_numa->numa_mask2, tlb_numa->pages1);
		}

		if (addr != MAP_FAILED) {
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_PAGEOUT)
			if (!tlb_numa->nopageout)
				(void)shim_madvise((void *)addr, size, SHIM_MADV_PAGEOUT);
#endif
			(void)munmap((void *)addr, size);
		}

		if (!tlb_numa->nombind) {
			stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);
			stress_tlb_numa_mbind(tlb_numa, node, tlb_numa->numa_mask2, tlb_numa->pages2);
			node = stress_numa_next_node(node, tlb_numa->numa_nodes);
			if (node < 0)
				node = 0;
		}
	} while (stress_bogo_inc_lock(tlb_numa->args, tlb_numa->lock, true));

	return &g_nowt;
}

/*
 *  stress_tlb_pthread3()
 *  	pthread3, exercise mmapping and many munmappings
 */
static void OPTIMIZE3 *stress_tlb_pthread3(void *parg)
{
	stress_tlb_numa_t *tlb_numa = (stress_tlb_numa_t *)parg;
	const size_t page_size = tlb_numa->page_size;
	const size_t page_size2 = 2 * page_size;
	uint32_t prev_cpu = 0;

	do {
		uint8_t *mmap3;
		const size_t n_pages = stress_mwc8modn(32) + 3;
		const size_t size = n_pages * page_size;

		mmap3 = mmap(NULL, size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (UNLIKELY(mmap3 == MAP_FAILED)) {
			stress_random_small_sleep();
			continue;
		} else {
			uint8_t *ptr, *ptr_end;

			stress_mmap_set_light(mmap3, size, page_size);

			stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);

			/* unmap odd pages */
			ptr = mmap3 + page_size;
			ptr_end = mmap3 + size;
			while (ptr < ptr_end) {
				(void)munmap((void *)ptr, page_size);
				ptr += page_size2;
			}

			stress_tlb_numa_change_cpu(tlb_numa->cpus, &prev_cpu);

			/* unmap even pages */
			ptr = mmap3;
			ptr_end = mmap3 + size;
			while (ptr < ptr_end) {
				(void)munmap((void *)ptr, page_size);
				ptr += page_size2;
			}
		}
	} while (stress_bogo_inc_lock(tlb_numa->args, tlb_numa->lock, true));

	return &g_nowt;
}

/*
 *  stress_tlb_numa_pages()
 *	unmap every other page to break mmap mmapping into pages
 */
static int stress_tlb_numa_pages(
	uint8_t *mmap,
	uint8_t **pages,
	const size_t mmap_pages,
	const size_t page_size)
{
	size_t i;
	uint8_t *ptr = mmap;

	for (i = 0; i < mmap_pages; i++, ptr += page_size * 2) {
		pages[i] = ptr;
		(void)munmap((void *)(ptr + page_size), page_size);
	}
	return 0;
}

/*
 *  stress_tlb_numa_pages_munmap()
 *  	unmap pages[]
 */
static void stress_tlb_numa_pages_munmap(
	uint8_t **pages,
	const size_t mmap_pages,
	const size_t page_size)
{
	size_t i;

	for (i = 0; i < mmap_pages; i++)
		(void)munmap((void *)pages[i], page_size);
}

/*
 *  stress_tlb_numa_pages_fill()
 *	fill mmap'd memory with data, different values per
 *	page to ensure KSM does not merge pages
 */
static void OPTIMIZE3 stress_tlb_numa_pages_fill(
	uint8_t *mmap,
	const size_t mmap_size,
	const size_t page_size)
{
	uint8_t *ptr;
	const uint8_t *ptr_end = mmap + mmap_size;
	uint8_t val = stress_mwc8();

	for (ptr = mmap; ptr < ptr_end; ptr += page_size) {
		if (val == 0)
			val++;
		(void)shim_memset(ptr, val, page_size);
		val++;
	}
}

static stress_tlb_pthread_t stress_tlb_pthreads[] = {
	stress_tlb_pthread1,
	stress_tlb_pthread2,
	stress_tlb_pthread3,
};

#define TLB_NUMA_PTHREADS	SIZEOF_ARRAY(stress_tlb_pthreads)

/*
 *  stress_tlb_numa()
 *	stress out TLB shootdowns
 */
static int stress_tlb_numa(stress_args_t *args)
{
	double rate, t_begin, duration;
	uint64_t tlb_begin, tlb_end;
	size_t tlb_entries = DEFAULT_TLB_NUMA_ENTRIES;
	size_t size;
	int rc = EXIT_SUCCESS;
	stress_tlb_numa_t tlb_numa;
	stress_tlb_numa_pthread_t pthreads[TLB_NUMA_PTHREADS];
	size_t i;
	uint32_t prev_cpu = 0;
	uint8_t	*mmap1;
	uint8_t *mmap2;
	long int node = 0;

#if defined(STRESS_ARCH_X86)
	uint32_t x86_tlb_entries;
	uint8_t x86_tlb_level;
#endif

	(void)shim_memset(&tlb_numa, 0, sizeof(tlb_numa));
	(void)stress_setting_get("tlb-numa-nombind", &tlb_numa.nombind);
	(void)stress_setting_get("tlb-numa-nopageout", &tlb_numa.nopageout);

	if (!stress_setting_get("tlb-numa-entries", &tlb_entries)) {
#if defined(STRESS_ARCH_X86)
		stress_cpu_x86_dtlb_entries(&x86_tlb_entries, &x86_tlb_level);
		if (x86_tlb_entries > 0) {
			tlb_entries = (size_t)x86_tlb_entries;
			if (stress_instance_zero(args)) {
				pr_inf("%s: detected %zu L%" PRIu8 " data TLB entries\n",
					args->name, tlb_entries, x86_tlb_level);
			}
		} else {
			if (stress_instance_zero(args)) {
				pr_inf("%s: defaulting to %zu data TLB entries\n",
					args->name, tlb_entries);
			}
		}
#else
		if (stress_instance_zero(args)) {
			pr_inf("%s: defaulting to %zu data TLB entries\n",
				args->name, tlb_entries);
		}
#endif
	}
	tlb_entries = (args->instances > 1) ? tlb_entries  / args->instances : tlb_entries;
	tlb_entries = (tlb_entries < MIN_TLB_NUMA_ENTRIES) ? MIN_TLB_NUMA_ENTRIES : tlb_entries;

	if (stress_instance_zero(args))
		pr_inf("%s: using %zu data TLB entries per instance\n", args->name, tlb_entries);

	tlb_numa.args = args;
	tlb_numa.page_size = args->page_size;
	tlb_numa.mmap_pages = tlb_entries;
	tlb_numa.mmap_size = args->page_size * tlb_numa.mmap_pages * 2;
	tlb_numa.cpus = (uint32_t)stress_cpus_configured_get();

	/*
	 *  allocate pages array to point to even mmap'd pages
	 */
	tlb_numa.pages1 = calloc(tlb_numa.mmap_pages, sizeof(uint8_t *));
	if (!tlb_numa.pages1) {
		pr_inf_skip("%s: failed to allocate %zu pointers%s, skipping stressor\n",
			args->name, tlb_numa.mmap_pages, stress_memory_free_get());
		return EXIT_NO_RESOURCE;
	}
	tlb_numa.pages2 = calloc(tlb_numa.mmap_pages, sizeof(uint8_t *));
	if (!tlb_numa.pages2) {
		pr_inf_skip("%s: failed to allocate %zu pointers%s, skipping stressor\n",
			args->name, tlb_numa.mmap_pages, stress_memory_free_get());
		rc = EXIT_NO_RESOURCE;
		goto free_pages1;
	}

	/*
	 *  get numa masks and numa node bitmaps
	 */
	tlb_numa.numa_nodes = stress_numa_mask_alloc();
	if (!tlb_numa.numa_nodes) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_pages2;
	}
	if (stress_numa_mask_nodes_get(tlb_numa.numa_nodes) < 1) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_nodes;
	}
	tlb_numa.numa_mask0 = stress_numa_mask_alloc();
	if (!tlb_numa.numa_mask0) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_nodes;
	}
	tlb_numa.numa_mask1 = stress_numa_mask_alloc();
	if (!tlb_numa.numa_mask1) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask0;
	}
	tlb_numa.numa_mask2 = stress_numa_mask_alloc();
	if (!tlb_numa.numa_mask2) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask1;
	}
	tlb_numa.numa_mask_ff = stress_numa_mask_alloc();
	if (!tlb_numa.numa_mask_ff) {
		pr_inf_skip("%s: no NUMA nodes found, skipping stressor\n", args->name);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask2;
	}

	/*
	 *  get mappings for arrays of even pages to be assigned to
	 */
	mmap1 = (uint8_t *)stress_tlb_numa_mmap(args, NULL, tlb_numa.mmap_size,
					PROT_WRITE | PROT_READ,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmap1 == MAP_FAILED) {
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask_ff;
	}
	mmap2 = (uint8_t *)stress_tlb_numa_mmap(args, NULL, tlb_numa.mmap_size,
					PROT_WRITE | PROT_READ,
					MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmap2 == MAP_FAILED) {
		(void)munmap((void *)mmap1, tlb_numa.mmap_size);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask_ff;
	}

	tlb_numa.lock = stress_lock_create("bogo-op-lock");
	if (!tlb_numa.lock) {
		pr_inf_skip("%s: bogo-op lock create, skipping stressor\n", args->name);
		(void)munmap((void *)mmap2, tlb_numa.mmap_size);
		(void)munmap((void *)mmap1, tlb_numa.mmap_size);
		rc = EXIT_NO_RESOURCE;
		goto free_numa_mask_ff;
	}

	/*
	 *  Ensure mappings are populated
	 */
	stress_tlb_numa_pages_fill(mmap1, tlb_numa.mmap_size, tlb_numa.page_size);
	stress_tlb_numa_pages_fill(mmap2, tlb_numa.mmap_size, tlb_numa.page_size);

	(void)shim_memset(tlb_numa.numa_mask_ff->mask, 0xff, tlb_numa.numa_mask_ff->mask_size);
	stress_tlb_numa_pages(mmap1, tlb_numa.pages1, tlb_numa.mmap_pages, tlb_numa.page_size);
	stress_tlb_numa_shuffle_pages(tlb_numa.pages1, tlb_numa.mmap_pages);
	stress_tlb_numa_pages(mmap2, tlb_numa.pages2, tlb_numa.mmap_pages, tlb_numa.page_size);
	stress_tlb_numa_shuffle_pages(tlb_numa.pages2, tlb_numa.mmap_pages);

	if (stress_instance_zero(args)) {
		const size_t usage = tlb_numa.mmap_size + (2 * tlb_numa.mmap_pages * sizeof(uint8_t *));

		stress_memory_usage_get(args, usage, usage * args->instances);
	}

	for (i = 0; i < TLB_NUMA_PTHREADS; i++) {
		pthreads[i].ret = pthread_create(&pthreads[i].pthread, NULL,
				stress_tlb_pthreads[i], (void *)&tlb_numa);
		if (UNLIKELY(pthreads[i].ret)) {
			pr_fail("%s: pthread_create failed, errno=%d (%s)\n",
				args->name, pthreads[i].ret, strerror(pthreads[i].ret));
			rc = EXIT_NO_RESOURCE;
			goto err_reap;
		}
	}

	t_begin = stress_time_now();
	tlb_begin = stress_interrupts_tlb();

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	size = tlb_numa.page_size * 2;
	do {
		uint8_t *addr;

		addr = (uint8_t *)stress_mmap_populate(NULL, size,
				PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
		if (addr != MAP_FAILED) {
			*(addr + 0) = 0x5a;
			*(addr + tlb_numa.page_size) = 0xa5;
		}

		stress_tlb_numa_change_cpu(tlb_numa.cpus, &prev_cpu);
		if (!tlb_numa.nombind && stress_tlb_numa_mbind_do()) {
			node = stress_numa_next_node(node, tlb_numa.numa_nodes);
			if (node < 0)
				node = 0;
			shim_memset(tlb_numa.numa_mask0->mask, 0x00, tlb_numa.numa_mask0->mask_size);
			STRESS_SETBIT(tlb_numa.numa_mask0->mask, node);
			(void)shim_mbind((void *)addr, size, MPOL_BIND, tlb_numa.numa_mask0->mask,
					tlb_numa.numa_mask0->max_nodes, MPOL_MF_STRICT);
		}

		if (addr != MAP_FAILED) {
#if defined(HAVE_MADVISE) &&	\
    defined(SHIM_MADV_PAGEOUT)
			if (!tlb_numa.nopageout)
				(void)shim_madvise((void *)addr, size, SHIM_MADV_PAGEOUT);
#endif
			(void)munmap((void *)addr, size);
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);
	tlb_end = stress_interrupts_tlb();
	duration = stress_time_now() - t_begin;

	rate = (duration > 0.0) ? (double)(tlb_end - tlb_begin) / duration : 0.0;
	if (rate > 0.0)
		stress_metrics_set(args, "TLB shootdowns/sec", rate, STRESS_METRIC_GEOMETRIC_MEAN);


err_reap:
	for (i = 0; i < TLB_NUMA_PTHREADS; i++) {
		if (pthreads[i].ret == 0)
			(void)pthread_cancel(pthreads[i].pthread);
	}

	stress_tlb_numa_pages_munmap(tlb_numa.pages2, tlb_numa.mmap_pages, tlb_numa.page_size);
	stress_tlb_numa_pages_munmap(tlb_numa.pages1, tlb_numa.mmap_pages, tlb_numa.page_size);

	stress_lock_destroy(tlb_numa.lock);
free_numa_mask_ff:
	stress_numa_mask_free(tlb_numa.numa_mask_ff);
free_numa_mask2:
	stress_numa_mask_free(tlb_numa.numa_mask2);
free_numa_mask1:
	stress_numa_mask_free(tlb_numa.numa_mask1);
free_numa_mask0:
	stress_numa_mask_free(tlb_numa.numa_mask0);
free_numa_nodes:
	stress_numa_mask_free(tlb_numa.numa_nodes);
free_pages2:
	(void)free(tlb_numa.pages2);
free_pages1:
	(void)free(tlb_numa.pages1);

	return rc;
}

const stressor_info_t stress_tlb_numa_info = {
	.stressor = stress_tlb_numa,
	.classifier = CLASS_TLB | CLASS_MEMORY,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts
};
#else
const stressor_info_t stress_tlb_numa_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_TLB | CLASS_MEMORY,
	.verify = VERIFY_NONE,
	.help = help,
	.opts = opts,
	.unimplemented_reason = "built without sched_setaffinity(), mprotect() or NUMA system calls"
};
#endif
