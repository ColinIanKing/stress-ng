/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

typedef struct {
	bool mmapfixed_mlock;
	bool mmapfixed_numa;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;
	stress_numa_mask_t *numa_nodes;
#endif
} mmapfixed_info_t;

static const stress_help_t help[] = {
	{ NULL,	"mmapfixed N",		"start N workers stressing mmap with fixed mappings" },
	{ NULL,	"mmapfixed-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"mmapfixed-numa",	"bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	"mmapfixed-ops N",	"stop after N mmapfixed bogo operations" },
	{ NULL,	NULL,			NULL }
};

#if UINTPTR_MAX == MAX_32
#define MMAP_TOP	(0x80000000UL)
#else
#define MMAP_TOP	(0x8000000000000000ULL)
#endif
#define MMAP_BOTTOM	(0x10000)

#define PAGE_CHUNKS	(1024)

/*
 *  stress_mmapfixed_is_mapped_slow()
 *	walk through region with mincore to see if any pages are mapped
 */
static bool OPTIMIZE3 stress_mmapfixed_is_mapped_slow(
	void *addr,
	size_t len,
	const size_t page_size)
{
	uint64_t vec[PAGE_CHUNKS / sizeof(uint64_t)] ALIGN64;
	ssize_t n = (ssize_t)len;
	size_t n_pages = len / page_size;

	if (n_pages > PAGE_CHUNKS)
		n_pages = PAGE_CHUNKS;

	(void)shim_memset(vec, 0, sizeof(vec));
	while (n > 0) {
		int ret;
		register const size_t sz = n_pages * page_size;
		register size_t j;

		n -= n_pages;
		ret = shim_mincore(addr, sz, (unsigned char *)vec);
		if (UNLIKELY(ret == ENOSYS))
			return false;	/* Dodgy, assume not in memory */

PRAGMA_UNROLL_N(4)
		for (j = 0; j < SIZEOF_ARRAY(vec); j++) {
			if (vec[j])
				return true;
		}
		addr = (void *)(((uintptr_t)addr) + sz);
	}
	return false;
}

/*
 *  stress_mmapfixed_is_mapped()
 *	check if region is memory mapped, try fast one mincore check first,
 *	then msync, then use slower multiple mincore calls
 */
static bool stress_mmapfixed_is_mapped(
	void *addr,
	size_t len,
	const size_t page_size)
{
	int ret;

	if (len > (page_size * PAGE_CHUNKS))
		return stress_mmapfixed_is_mapped_slow(addr, len, page_size);
	ret = shim_msync(addr, len, 0);
	if (ret == ENOSYS)
		return stress_mmapfixed_is_mapped_slow(addr, len, page_size);
	if (ret == 0)
		return true;
	return stress_mmapfixed_is_mapped_slow(addr, len, page_size);
}

static int stress_mmapfixed_child(stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2,4,0) && 	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)
	const uintptr_t page_mask = ~((uintptr_t)(page_size - 1));
#endif
	uintptr_t addr = MMAP_TOP;
	int rc = EXIT_SUCCESS;
	mmapfixed_info_t *info = (mmapfixed_info_t *)context;

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV,
				stress_sig_handler_exit, NULL));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint8_t *buf;
		int flags = MAP_FIXED | MAP_ANONYMOUS;
		const size_t sz = page_size * (1 + stress_mwc8modn(7));

#if defined(MAP_SHARED) &&	\
    defined(MAP_PRIVATE)
		flags |= stress_mwc1() ? MAP_SHARED : MAP_PRIVATE;
#endif
#if defined(MAP_LOCKED)
		flags |= stress_mwc1() ? MAP_LOCKED : 0;
#endif
#if defined(MAP_NORESERVE)
		flags |= stress_mwc1() ? MAP_NORESERVE : 0;
#endif
#if defined(MAP_POPULATE)
		flags |= stress_mwc1() ? MAP_POPULATE : 0;
#endif
#if defined(MAP_FIXED_NOREPLACE)
		/* 4.17 Linux flag */
		flags &= ~MAP_FIXED;
		flags |= stress_mwc1() ? MAP_FIXED : MAP_FIXED_NOREPLACE;
#endif

		if (UNLIKELY(!stress_continue_flag()))
			break;

		if (stress_mmapfixed_is_mapped((void *)addr, sz, page_size))
			goto next;

		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(sz))
			goto next;

		buf = (uint8_t *)mmap((void *)addr, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED)
			goto next;
#if defined(HAVE_LINUX_MEMPOLICY_H)
		if (info->mmapfixed_numa)
			stress_numa_randomize_pages(args, info->numa_nodes, info->numa_mask, buf, sz, page_size);
#endif
		if (info->mmapfixed_mlock)
			(void)shim_mlock(buf, sz);
		(void)stress_madvise_randomize(buf, sz);
#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2,4,0) && 	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)
		{
			uint8_t *newbuf;
			uintptr_t mask = ~(uintptr_t)0;
			const uintptr_t newaddr = addr ^
				((page_size << 3) | (page_size << 4));
#if UINTPTR_MAX == MAX_32
			const uintptr_t rndaddr_base = (uintptr_t)stress_mwc32() & page_mask;
#else
			const uintptr_t rndaddr_base = (uintptr_t)stress_mwc64() & page_mask;
#endif
			uintptr_t last_rndaddr = 0;

			if (stress_mmapfixed_is_mapped((void *)newaddr, sz, page_size))
				goto unmap;
			newbuf = mremap(buf, sz, sz,
					MREMAP_FIXED | MREMAP_MAYMOVE,
					(void *)newaddr);
			if (newbuf && (newbuf != MAP_FAILED))
				buf = newbuf;

#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (info->mmapfixed_numa)
				stress_numa_randomize_pages(args, info->numa_nodes, info->numa_mask, buf, sz, page_size);
#endif
			if (info->mmapfixed_mlock)
				(void)shim_mlock(buf, sz);
			(void)stress_madvise_randomize(buf, sz);

			for (mask = ~(uintptr_t)0; mask > page_size; mask >>= 1) {
				const uintptr_t rndaddr = rndaddr_base & mask;
				uint64_t *buf64 = (uint64_t *)buf;
				const uint64_t val64 = (uint64_t)(uintptr_t)buf64;

				if (rndaddr == last_rndaddr)
					continue;
				last_rndaddr = rndaddr;

				if (rndaddr <= page_size)
					break;
				if (stress_mmapfixed_is_mapped((void *)rndaddr, sz, page_size))
					continue;

				*buf64 = val64;
				newbuf = mremap(buf, sz, sz,
						MREMAP_FIXED | MREMAP_MAYMOVE,
						(void *)rndaddr);
				if (newbuf && (newbuf != MAP_FAILED)) {
					buf64 = (uint64_t *)newbuf;

					if (UNLIKELY(*buf64 != val64)) {
						pr_fail("%s: remap from %p to %p contains 0x%" PRIx64
							" and not expected value 0x%" PRIx64 "\n",
							args->name, buf, newbuf, *buf64, val64);
						rc = EXIT_FAILURE;
					}

					buf = newbuf;
#if defined(HAVE_LINUX_MEMPOLICY_H)
					if (info->mmapfixed_numa)
						stress_numa_randomize_pages(args, info->numa_nodes, info->numa_mask, buf, sz, page_size);
#endif
					if (info->mmapfixed_mlock)
						(void)shim_mlock(buf, sz);
					(void)stress_madvise_randomize(buf, sz);
				}
			}
		}
unmap:
#endif
		(void)stress_munmap_force((void *)buf, sz);
		stress_bogo_inc(args);
next:
		addr >>= 1;
		if (addr < MMAP_BOTTOM)
			addr = MMAP_TOP;
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

/*
 *  stress_mmapfixed()
 *	stress mmap at fixed hinted addresses
 */
static int stress_mmapfixed(stress_args_t *args)
{
	mmapfixed_info_t info;
	int ret;

	info.mmapfixed_mlock = false;
	info.mmapfixed_numa = false;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	info.numa_mask = NULL;
	info.numa_nodes = NULL;
#endif

	(void)stress_get_setting("mmapfixed-mlock", &info.mmapfixed_mlock);
	(void)stress_get_setting("mmapfixed-numa", &info.mmapfixed_numa);

	if (info.mmapfixed_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &info.numa_nodes,
						&info.numa_mask, "--mmapfixed-numa",
						&info.mmapfixed_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --mmapfixed-numa selected but not supported by this system, disabling option\n",
				args->name);
		info.mmapfixed_numa = false;
#endif
	}
	ret = stress_oomable_child(args, &info, stress_mmapfixed_child, STRESS_OOMABLE_QUIET);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (info.numa_mask)
		stress_numa_mask_free(info.numa_mask);
	if (info.numa_nodes)
		stress_numa_mask_free(info.numa_nodes);
#endif

	return ret;
}

static const stress_opt_t opts[] = {
	{ OPT_mmapfixed_mlock, "mmapfixed-mlock", TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_mmapfixed_numa,  "mmapfixed-numa", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_mmapfixed_info = {
	.stressor = stress_mmapfixed,
	.classifier = CLASS_VM | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
