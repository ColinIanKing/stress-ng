/*
 * Copyright (C) 2022-2023 Colin Ian King
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
#include "core-asm-ppc64.h"
#include "core-asm-x86.h"
#include "core-cache.h"
#include "core-icache.h"

static const stress_help_t help[] = {
	{ NULL,	"flushcache N",		"start N CPU instruction + data cache flush workers" },
	{ NULL,	"flushcache-ops N",	"stop after N flush cache bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if (defined(STRESS_ARCH_X86) ||	\
     defined(STRESS_ARCH_ARM) ||	\
     defined(STRESS_ARCH_RISCV) ||	\
     defined(STRESS_ARCH_S390) ||	\
     defined(STRESS_ARCH_PPC64)) &&	\
     defined(__GNUC__) && 		\
     NEED_GNUC(4,6,0) &&		\
     defined(HAVE_MPROTECT)

typedef void (*icache_func_ptr)(void);

static int stress_flushcache_nohugepage(void *addr, size_t size)
{
#if defined(MADV_NOHUGEPAGE)
	(void)shim_madvise((void *)addr, size, MADV_NOHUGEPAGE);
#else
	(void)addr;
	(void)size;
#endif
	return 0;
}

static int stress_flushcache_mprotect(
	const stress_args_t *args,
	void *addr,
	size_t size,
	int prot)
{
	int ret;

	ret = mprotect(addr, size, prot);
	if (ret < 0) {
		pr_inf("%s: mprotect failed on text page %p: errno=%d (%s)\n",
			args->name, addr, errno, strerror(errno));
	}
	return ret;
}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
/*
 *  clear_cache_page()
 *	clear a page using repeated clear cache calls
 */
static void clear_cache_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		(*(volatile uint8_t *)ptr)--;
		__builtin___clear_cache((void *)ptr, (void *)(ptr + cl_size));
		ptr += cl_size;
	}
}
#endif

#if defined(HAVE_ASM_PPC64_DCBST)
static inline void dcbst_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		(*(volatile uint8_t *)ptr)--;
		stress_asm_ppc64_dcbst((void *)ptr);
		ptr += cl_size;
	}
}
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
static inline void cldemote_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		(*(volatile uint8_t *)ptr)--;
		stress_asm_x86_cldemote((void *)ptr);
		ptr += cl_size;
	}
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static inline void clflush_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		(*(volatile uint8_t *)ptr)--;
		stress_asm_x86_clflush((void *)ptr);
		ptr += cl_size;
	}
}
#endif

/*
 *  stress_flush_icache()
 *	macro to generate functions that stress instruction cache
 *	load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
static inline int stress_flush_icache(
	const stress_args_t *args,
	void *addr,
	const size_t page_size,
	const size_t cl_size,
	const size_t i_size,
	icache_func_ptr icache_func)
{
	void *page_addr = (void *)((uintptr_t)addr & ~(page_size - 1));
	uint8_t *ptr = (uint8_t *)addr;
	uint8_t *ptr_end = ptr + i_size;

	if (stress_flushcache_mprotect(args, page_addr, i_size, PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		return -1;

	while ((ptr < ptr_end) && keep_stressing_flag()) {
		volatile uint8_t *vptr = (volatile uint8_t *)ptr;
		uint8_t val;

		val = *vptr;
		*vptr ^= ~0;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#endif
		*vptr = val;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#endif
		ptr += cl_size;
	}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	clear_cache_page(addr, i_size, cl_size);
#endif
	(void)shim_cacheflush((char *)addr, i_size, SHIM_ICACHE);
	if (stress_flushcache_mprotect(args, page_addr, i_size, PROT_READ | PROT_EXEC) < 0)
		return -1;

	icache_func();

	return 0;
}

static inline int stress_flush_dcache(
	const stress_args_t *args,
	void *addr,
	const size_t page_size,
	const size_t cl_size,
	const size_t d_size,
	const bool x86_clfsh,
	const bool x86_demote)
{
	(void)args;

	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + d_size;

	(void)cl_size;

	while ((ptr < ptr_end) && keep_stressing_flag()) {
#if defined(HAVE_ASM_X86_CLFLUSH)
		if (x86_clfsh)
			clflush_page((void *)ptr, page_size, cl_size);
#else
		(void)x86_clfsh;
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
		if (x86_demote)
			cldemote_page((void *)ptr, page_size, cl_size);
#else
		(void)x86_demote;
#endif
#if defined(HAVE_ASM_PPC64_DCBST)
		dcbst_page((void *)ptr, page_size, cl_size);
#endif
		shim_cacheflush((void *)ptr, page_size, SHIM_DCACHE);
		ptr += page_size;
	}
	return 0;
}

/*
 *  stress_flushcache()
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
static int stress_flushcache(const stress_args_t *args)
{
	const size_t page_size = args->page_size;

	void *d_addr, *i_addr;
	size_t d_size, i_size, cl_size;
	const bool x86_clfsh = stress_cpu_x86_has_clfsh();
	const bool x86_demote = stress_cpu_x86_has_cldemote();
	icache_func_ptr icache_func;

	switch (page_size) {
	case SIZE_4K:
		icache_func = stress_icache_func_4K;
		break;
	case SIZE_16K:
		icache_func = stress_icache_func_16K;
		break;
#if defined(HAVE_ALIGNED_64K)
	case SIZE_64K:
		icache_func = stress_icache_func_64K;
		break;
#endif
	default:
#if defined(HAVE_ALIGNED_64K)
		pr_inf_skip("%s: page size %zu is not %u or %u or %u, cannot test, skipping stressor\n",
			args->name, args->page_size,
			SIZE_4K, SIZE_16K, SIZE_64K);
#else
		pr_inf_skip("%s: page size %zu is not %u or %u, cannot test, skipping stressor\n",
			args->name, args->page_size,
			SIZE_4K, SIZE_16K);
#endif
		return EXIT_NO_RESOURCE;
	}

	i_size = page_size;
	i_addr = (void *)icache_func;

	stress_get_llc_size(&d_size, &cl_size);
	if (d_size < page_size)
		d_size = page_size;
	if (cl_size == 0)
		cl_size = 64;

	d_addr = mmap(NULL, d_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (d_addr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zd bytes, skipping stressor\n",
			args->name, d_size);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_flushcache_nohugepage(i_addr, i_size);
	(void)stress_flushcache_nohugepage(d_addr, d_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_flush_icache(args, i_addr, page_size, cl_size, i_size, icache_func);
		stress_flush_dcache(args, d_addr, page_size, cl_size, d_size, x86_clfsh, x86_demote);

		shim_cacheflush(i_addr, i_size, SHIM_ICACHE | SHIM_DCACHE);
		shim_cacheflush(d_addr, d_size, SHIM_ICACHE | SHIM_DCACHE);

		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(d_addr, page_size);

	return EXIT_SUCCESS;
}

stressor_info_t stress_flushcache_info = {
	.stressor = stress_flushcache,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#else
stressor_info_t stress_flushcache_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU_CACHE,
	.help = help,
	.unimplemented_reason = "built without cache flush support"
};
#endif
