/*
 * Copyright (C) 2022-2025 Colin Ian King
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
#include "core-asm-ret.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-numa.h"
#include "core-out-of-memory.h"

static const stress_help_t help[] = {
	{ NULL,	"flushcache N",		"start N CPU instruction + data cache flush workers" },
	{ NULL,	"flushcache-ops N",	"stop after N flush cache bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if (defined(STRESS_ARCH_X86) ||		\
     defined(STRESS_ARCH_ARM) ||	\
     defined(STRESS_ARCH_RISCV) ||	\
     defined(STRESS_ARCH_S390) ||	\
     defined(STRESS_ARCH_PPC) ||	\
     defined(STRESS_ARCH_PPC64)) &&	\
     defined(HAVE_MPROTECT) &&		\
     ((defined(HAVE_COMPILER_GCC_OR_MUSL) && NEED_GNUC(4,6,0)) ||	\
      (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(9,0,0)) || 		\
      (defined(HAVE_COMPILER_ICX) && NEED_ICX(2023,2,0)) ||		\
      (defined(HAVE_COMPILER_ICC) && NEED_ICC(2021,0,0)))

typedef struct {
	stress_ret_func_t icache_func;	/* 4K/16K/64K sized i-cache function */
	void	*d_addr;		/* data cache address */
	void	*i_addr;		/* instruction cache address */
	size_t	d_size;			/* data cache size */
	size_t 	i_size;			/* instruction cache size */
	size_t	cl_size;		/* cache line size */
	bool	x86_clfsh;		/* true if x86 clflush op is available */
	bool	x86_demote;		/* true if x86 cldemote op is available */
} stress_flushcache_context_t;

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
	stress_args_t *args,
	void *addr,
	size_t size,
	int prot)
{
	int ret;

	ret = mprotect(addr, size, prot);
	if (ret < 0) {
		pr_inf("%s: mprotect failed on text page %p, errno=%d (%s)\n",
			args->name, addr, errno, strerror(errno));
	}
	return ret;
}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
/*
 *  clear_cache_page()
 *	clear a page using repeated clear cache calls
 */
static inline void clear_cache_page(
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

#if defined(STRESS_ARCH_PPC64) &&	\
    defined(HAVE_ASM_PPC64_DCBST)
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

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_ASM_PPC_DCBST)
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
		stress_asm_ppc_dcbst((void *)ptr);
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
	stress_args_t *args,
	const stress_flushcache_context_t *context)
{
	void *i_addr = context->i_addr;
	const size_t i_size = context->i_size;
	const size_t cl_size = context->cl_size;
	void *page_addr = (void *)((uintptr_t)i_addr & ~(args->page_size - 1));
	uint8_t *ptr = (uint8_t *)i_addr;
	uint8_t *ptr_end = ptr + i_size;

	if (stress_flushcache_mprotect(args, page_addr, context->i_size, PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		return -1;

	while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
		volatile uint8_t *vptr = (volatile uint8_t *)ptr;
		const uint8_t val = *vptr;

		*vptr ^= ~0;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#elif defined(HAVE_ASM_PPC_ICBI)
		stress_asm_ppc_icbi((void *)ptr);
#endif
		*vptr = val;
		shim_flush_icache((char *)ptr, (char *)ptr + cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#elif defined(HAVE_ASM_PPC_ICBI)
		stress_asm_ppc_icbi((void *)ptr);
#endif
		ptr += cl_size;
	}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	clear_cache_page(i_addr, i_size, cl_size);
#endif
	(void)shim_cacheflush((char *)i_addr, i_size, SHIM_ICACHE);
	if (stress_flushcache_mprotect(args, page_addr, i_size, PROT_READ | PROT_EXEC) < 0)
		return -1;

	context->icache_func();

	return 0;
}

static inline int stress_flush_dcache(
	stress_args_t *args,
	const stress_flushcache_context_t *context)
{
	void *d_addr = context->d_addr;
	const size_t d_size = context->d_size;
	const size_t page_size = args->page_size;
#if defined(HAVE_ASM_X86_CLFLUSH) ||	\
    defined(HAVE_ASM_X86_CLDEMOTE) ||	\
    defined(HAVE_ASM_PPC_DCBST) ||	\
    defined(HAVE_ASM_PPC64_DCBST)
	const size_t cl_size = context->cl_size;
#endif

	register uint8_t *ptr = (uint8_t *)d_addr;
	const uint8_t *ptr_end = ptr + d_size;

	while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
#if defined(HAVE_ASM_X86_CLFLUSH)
		if (context->x86_clfsh)
			clflush_page((void *)ptr, page_size, cl_size);
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
		if (context->x86_demote)
			cldemote_page((void *)ptr, page_size, cl_size);
#endif
#if defined(HAVE_ASM_PPC_DCBST) ||	\
    defined(HAVE_ASM_PPC64_DCBST)
		dcbst_page((void *)ptr, page_size, cl_size);
#endif
		shim_cacheflush((void *)ptr, page_size, SHIM_DCACHE);
		ptr += page_size;
	}
	return 0;
}

static int stress_flushcache_child(stress_args_t *args, void *ctxt)
{
	stress_flushcache_context_t *context = (stress_flushcache_context_t *)ctxt;

	context->d_addr = stress_mmap_populate(NULL, context->d_size,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (context->d_addr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, skipping stressor\n",
			args->name, context->d_size, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context->d_addr, context->d_size, "d-cache");

	if (context->i_addr)
		(void)stress_flushcache_nohugepage(context->i_addr, context->i_size);
	(void)stress_flushcache_nohugepage(context->d_addr, context->d_size);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (context->i_addr)
			stress_flush_icache(args, context);
		stress_flush_dcache(args, context);

		if (context->i_addr)
			shim_cacheflush(context->i_addr, context->i_size, SHIM_ICACHE | SHIM_DCACHE);
		shim_cacheflush(context->d_addr, context->d_size, SHIM_ICACHE | SHIM_DCACHE);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(context->d_addr, args->page_size);

	return EXIT_SUCCESS;
}

/*
 *  stress_flushcache()
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
static int stress_flushcache(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	const int numa_nodes = stress_numa_nodes();
	stress_flushcache_context_t context;
	int ret;

	context.x86_clfsh = stress_cpu_x86_has_clfsh();
	context.x86_demote = stress_cpu_x86_has_cldemote();
	context.i_addr = stress_mmap_populate(NULL, page_size,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (context.i_addr == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap %zu sized page%s, skipping stressor\n",
			args->name, page_size, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context.i_addr, page_size, "i-cache");
	context.icache_func = (stress_ret_func_t)context.i_addr;
	context.i_size = page_size;

	(void)shim_memcpy(context.i_addr, &stress_ret_opcode.opcodes, stress_ret_opcode.len);

	stress_cpu_cache_get_llc_size(&context.d_size, &context.cl_size);
	if (context.d_size < page_size)
		context.d_size = page_size;
	if (context.cl_size == 0)
		context.cl_size = 64;

	context.d_size *= numa_nodes;
	if (stress_instance_zero(args) && (numa_nodes > 1))
		pr_inf("%s: scaling cache size by number of numa nodes %d to %zuK\n",
			args->name, numa_nodes, context.d_size / 1024);
	ret = stress_oomable_child(args, (void *)&context, stress_flushcache_child, STRESS_OOMABLE_NORMAL);

	(void)munmap(context.i_addr, page_size);
	return ret;
}

const stressor_info_t stress_flushcache_info = {
	.stressor = stress_flushcache,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.help = help
};
#else
const stressor_info_t stress_flushcache_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.help = help,
	.unimplemented_reason = "built without cache flush support"
};
#endif
