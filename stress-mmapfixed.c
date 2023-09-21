// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"

static const stress_help_t help[] = {
	{ NULL,	"mmapfixed N",		"start N workers stressing mmap with fixed mappings" },
	{ NULL,	"mmapfixed-mlock",	"attempt to mlock pages into memory" },
	{ NULL,	"mmapfixed-ops N",	"stop after N mmapfixed bogo operations" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_mmapfixed_mlock(const char *opt)
{
	return stress_set_setting_true("mmapfixed-mlock", opt);
}

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
	unsigned char vec[PAGE_CHUNKS] ALIGN64;
	ssize_t n = (ssize_t)len;
	size_t n_pages = len / page_size;

	if (n_pages > PAGE_CHUNKS)
		n_pages = PAGE_CHUNKS;

	while (n > 0) {
		int ret;
		register size_t sz, j;


		sz = n_pages * page_size;
		n -= n_pages;

		(void)shim_memset(vec, 0, PAGE_CHUNKS);
		ret = shim_mincore(addr, sz, vec);
		if (UNLIKELY(ret == ENOSYS))
			return false;	/* Dodgy, assume not in memory */

PRAGMA_UNROLL_N(4)
		for (j = 0; j < n_pages; j++) {
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

static int stress_mmapfixed_child(const stress_args_t *args, void *context)
{
	const size_t page_size = args->page_size;
#if defined(HAVE_MREMAP) &&	\
    NEED_GLIBC(2,4,0) && 	\
    defined(MREMAP_FIXED) &&	\
    defined(MREMAP_MAYMOVE)
	const uintptr_t page_mask = ~((uintptr_t)(page_size - 1));
#endif
	uintptr_t addr = MMAP_TOP;
	bool mmapfixed_mlock = false;

	(void)context;

	(void)stress_get_setting("mmapfixed-mlock", &mmapfixed_mlock);

	VOID_RET(int, stress_sighandler(args->name, SIGSEGV,
				stress_sig_handler_exit, NULL));

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint8_t *buf;
		int flags = MAP_FIXED | MAP_ANONYMOUS;
		size_t  sz = page_size * (1 + stress_mwc8modn(7));

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

		if (!stress_continue_flag())
			break;

		if (stress_mmapfixed_is_mapped((void *)addr, sz, page_size))
			goto next;

		if ((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(sz))
			goto next;

		buf = (uint8_t *)mmap((void *)addr, sz, PROT_READ | PROT_WRITE, flags, -1, 0);
		if (buf == MAP_FAILED)
			goto next;

		if (mmapfixed_mlock)
			(void)shim_mlock(buf, sz);
		(void)stress_madvise_random(buf, sz);
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

			if (mmapfixed_mlock)
				(void)shim_mlock(buf, sz);
			(void)stress_madvise_random(buf, sz);

			for (mask = ~(uintptr_t)0; mask > page_size; mask >>= 1) {
				uintptr_t rndaddr = rndaddr_base & mask;
				uint64_t *buf64 = (uint64_t *)buf;
				uint64_t val64 = (uint64_t)(uintptr_t)buf64;

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

					if (*buf64 != val64) {
						pr_fail("%s: remap from %p to %p contains 0x%" PRIx64
							" and not expected value 0x%" PRIx64 "\n",
							args->name, buf, newbuf, *buf64, val64);
					}

					buf = newbuf;
					if (mmapfixed_mlock)
						(void)shim_mlock(buf, sz);
					(void)stress_madvise_random(buf, sz);
				}
			}
		}
unmap:
#endif
		(void)stress_munmap_retry_enomem((void *)buf, sz);
		stress_bogo_inc(args);
next:
		addr >>= 1;
		if (addr < MMAP_BOTTOM)
			addr = MMAP_TOP;
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

/*
 *  stress_mmapfixed()
 *	stress mmap at fixed hinted addresses
 */
static int stress_mmapfixed(const stress_args_t *args)
{
	return stress_oomable_child(args, NULL, stress_mmapfixed_child, STRESS_OOMABLE_QUIET);
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_mmapfixed_mlock,	stress_set_mmapfixed_mlock },
	{ 0,			NULL },
};

stressor_info_t stress_mmapfixed_info = {
	.stressor = stress_mmapfixed,
	.class = CLASS_VM | CLASS_OS,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
