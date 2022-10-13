/*
 * Copyright (C)      2022 Colin Ian King
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
#include "core-cache.h"

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

static void SECTION(stress_flushcache_callee) ALIGNED(4096)
stress_icache_func(void)
{
        return;
}

static int stress_flushcache_nohugepage(void *addr, size_t size)
{
#if defined(MADV_NOHUGEPAGE)
	(void)shim_madvise((void *)addr, size, MADV_NOHUGEPAGE);
#else
	(void)args;
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
		__builtin___clear_cache((void *)ptr, (void *)(ptr + cl_size));
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
	const size_t cl_size)
{
	void *page_addr = (void *)((uintptr_t)addr & ~(page_size - 1));
	uint8_t *ptr = (uint8_t *)addr;
	uint8_t *ptr_end = ptr + page_size;

	if (stress_flushcache_mprotect(args, page_addr, page_size, PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		return -1;

	while (ptr < ptr_end) {
		volatile uint8_t *vptr = (volatile uint8_t *)ptr;
		uint8_t val;

		val = *vptr;
		shim_mb();
		*vptr ^= ~0;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);
		*vptr = val;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);

		ptr += cl_size;
	}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	clear_cache_page(addr, page_size, cl_size);
#endif
	if (stress_flushcache_mprotect(args, page_addr, page_size, PROT_READ | PROT_EXEC) < 0)
		return -1;

	(void)shim_cacheflush((char *)addr, page_size, SHIM_ICACHE);
	stress_icache_func();

	return 0;
}

#if defined(HAVE_ASM_X86_CLFLUSH)
static inline void clflush(void *p)
{
        __asm__ __volatile__("clflush (%0)\n" : : "r"(p) : "memory");
}

static inline void clflush_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		clflush((void *)ptr);
		ptr += cl_size;
	}
}
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
static inline void cldemote(void *p)
{
        __asm__ __volatile__("cldemote (%0)\n" : : "r"(p) : "memory");
}

static inline void cldemote_page(
	void *addr,
	const size_t page_size,
	const size_t cl_size)
{
	register uint8_t *ptr = (uint8_t *)addr;
	const uint8_t *ptr_end = ptr + page_size;

	while (ptr < ptr_end) {
		(*(volatile uint8_t *)ptr)++;
		cldemote((void *)ptr);
		ptr += cl_size;
	}
}
#endif

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

	while (ptr < ptr_end) {
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
		shim_cacheflush((void *)ptr, page_size, SHIM_DCACHE);
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
		clear_cache_page((void *)ptr, page_size, cl_size);
#endif
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

	void *i_addr = (void *)stress_icache_func;
	void *d_addr;
	const bool x86_clfsh = stress_cpu_x86_has_clfsh();
	const bool x86_demote = stress_cpu_x86_has_cldemote();
	size_t d_size, cl_size;

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

	(void)stress_flushcache_nohugepage(i_addr, page_size);
	(void)stress_flushcache_nohugepage(d_addr, d_size);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		stress_flush_icache(args, i_addr, page_size, cl_size);
		stress_flush_dcache(args, d_addr, page_size, cl_size,
			d_size, x86_clfsh, x86_demote);

		shim_cacheflush(i_addr, page_size, SHIM_ICACHE | SHIM_DCACHE);
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
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#endif
