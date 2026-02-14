/*
 * Copyright (C) 2022-2026 Colin Ian King
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

#define MIN_D_BYTES	(1 * KB)
#define MAX_D_BYTES	(4 * GB)

#define MIN_I_BYTES	(1 * KB)
#define MAX_I_BYTES	(4 * GB)
#define DEFAULT_I_BYTES	(4 * KB)

static const stress_help_t help[] = {
	{ NULL,	"flushcache N",       "start N CPU instruction + data cache flush workers" },
	{ NULL, "flushcashe-d-bytes", "specify data cache size" },
	{ NULL, "flushcashe-i-bytes", "specify instruction cache size" },
	{ NULL,	"flushcache-ops N",   "stop after N flush cache bogo operations" },
	{ NULL,	NULL,                 NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_flushcache_d_bytes, "flushcache-d-bytes",  TYPE_ID_SIZE_T_BYTES_VM, MIN_D_BYTES, MAX_D_BYTES, NULL },
	{ OPT_flushcache_i_bytes, "flushcache-i-bytes",  TYPE_ID_SIZE_T_BYTES_VM, MIN_I_BYTES, MAX_I_BYTES, NULL },
	END_OPT,
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
	size_t	d_bytes;		/* data cache size */
	size_t 	i_bytes;		/* instruction cache size */
	size_t	d_cl_size;		/* data cache line size */
	size_t	i_cl_size;		/* instruction cache line size */
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
	const size_t i_bytes = context->i_bytes;
	const size_t d_cl_size = context->d_cl_size;
	uint8_t *ptr = (uint8_t *)i_addr;
	uint8_t *ptr_end = ptr + i_bytes;

	if (stress_flushcache_mprotect(args, i_addr, i_bytes, PROT_READ | PROT_WRITE | PROT_EXEC) < 0)
		return -1;

	while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
		volatile uint8_t *vptr = (volatile uint8_t *)ptr;
		const uint8_t val = *vptr;

		*vptr ^= ~0;
		shim_flush_icache((char *)ptr, (char *)ptr + d_cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#elif defined(HAVE_ASM_PPC_ICBI)
		stress_asm_ppc_icbi((void *)ptr);
#endif
		*vptr = val;
		shim_flush_icache((char *)ptr, (char *)ptr + d_cl_size);
#if defined(HAVE_ASM_PPC64_ICBI)
		stress_asm_ppc64_icbi((void *)ptr);
#elif defined(HAVE_ASM_PPC_ICBI)
		stress_asm_ppc_icbi((void *)ptr);
#endif
		ptr += d_cl_size;
	}

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	clear_cache_page(i_addr, i_bytes, d_cl_size);
#endif
	(void)shim_cacheflush((char *)i_addr, (int)i_bytes, SHIM_ICACHE);
	if (stress_flushcache_mprotect(args, i_addr, i_bytes, PROT_READ | PROT_EXEC) < 0)
		return -1;

	context->icache_func();

	return 0;
}

static inline int stress_flush_dcache(
	stress_args_t *args,
	const stress_flushcache_context_t *context)
{
	void *d_addr = context->d_addr;
	const size_t d_bytes= context->d_bytes;
	const size_t page_size = args->page_size;
#if defined(HAVE_ASM_X86_CLFLUSH) ||	\
    defined(HAVE_ASM_X86_CLDEMOTE) ||	\
    defined(HAVE_ASM_PPC_DCBST) ||	\
    defined(HAVE_ASM_PPC64_DCBST)
	const size_t d_cl_size = context->d_cl_size;
#endif

	register uint8_t *ptr = (uint8_t *)d_addr;
	const uint8_t *ptr_end = ptr + d_bytes;

	while (LIKELY((ptr < ptr_end) && stress_continue_flag())) {
#if defined(HAVE_ASM_X86_CLFLUSH)
		if (context->x86_clfsh)
			clflush_page((void *)ptr, page_size, d_cl_size);
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
		if (context->x86_demote)
			cldemote_page((void *)ptr, page_size, d_cl_size);
#endif
#if defined(HAVE_ASM_PPC_DCBST) ||	\
    defined(HAVE_ASM_PPC64_DCBST)
		dcbst_page((void *)ptr, page_size, d_cl_size);
#endif
		shim_cacheflush((char *)ptr, (int)page_size, SHIM_DCACHE);
		ptr += page_size;
	}
	return 0;
}

static int stress_flushcache_child(stress_args_t *args, void *ctxt)
{
	stress_flushcache_context_t *context = (stress_flushcache_context_t *)ctxt;

	context->d_addr = stress_mmap_populate(NULL, context->d_bytes,
			PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (context->d_addr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu bytes%s, skipping stressor\n",
			args->name, context->d_bytes, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context->d_addr, context->d_bytes, "d-cache");

	if (context->i_addr)
		(void)stress_flushcache_nohugepage(context->i_addr, context->i_bytes);
	(void)stress_flushcache_nohugepage(context->d_addr, context->d_bytes);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (context->i_addr)
			stress_flush_icache(args, context);
		stress_flush_dcache(args, context);

		if (context->i_addr)
			shim_cacheflush((char *)context->i_addr, (int)context->i_bytes, SHIM_ICACHE | SHIM_DCACHE);
		shim_cacheflush((char *)context->d_addr, (int)context->d_bytes, SHIM_ICACHE | SHIM_DCACHE);

		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(context->d_addr, context->d_bytes);

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
	long int numa_nodes = stress_numa_nodes();
	int ret;
	stress_flushcache_context_t context;

	(void)shim_memset(&context, 0, sizeof(context));
	context.i_bytes = page_size;

	stress_cpu_cache_llc_size_get(&context.d_bytes, &context.d_cl_size);
	stress_cpu_cache_get_level_size(1, &context.i_bytes, &context.i_cl_size, CACHE_TYPE_INSTRUCTION);

	(void)stress_get_setting("flushcache-d-bytes", &context.d_bytes);
	(void)stress_get_setting("flushcache-i-bytes", &context.i_bytes);

	if (context.d_bytes < page_size)
		context.d_bytes = page_size;
	if (context.i_bytes < page_size)
		context.i_bytes = page_size;
	if (context.d_cl_size == 0)
		context.d_cl_size = 64;
	if (context.i_cl_size == 0)
		context.i_cl_size = 64;

	if (UNLIKELY(numa_nodes < 1))
		numa_nodes = 1;

	context.x86_clfsh = stress_cpu_x86_has_clfsh();
	context.x86_demote = stress_cpu_x86_has_cldemote();
	context.i_addr = stress_mmap_anon_shared(context.i_bytes, PROT_READ | PROT_WRITE | PROT_EXEC);
	if (context.i_addr == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap %zu sized page%s, skipping stressor\n",
			args->name, context.i_bytes, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context.i_addr, context.i_bytes, "i-cache");
	context.icache_func = (stress_ret_func_t)context.i_addr;

	(void)shim_memcpy(context.i_addr, &stress_ret_opcode.opcodes, stress_ret_opcode.len);


	context.d_bytes *= numa_nodes;
	if (stress_instance_zero(args)) {
		char d_str[32], i_str[32];

		if (numa_nodes > 1) {
			pr_inf("%s: scaling data cache size by number of numa nodes %ld to %zuK\n",
				args->name, numa_nodes, context.d_bytes >> 10);
		}
		pr_inf("%s: data cache size: %s, instruction cache size: %s\n",
			args->name,
			stress_uint64_to_str(d_str, sizeof(d_str), (uint64_t)context.d_bytes, 2, true),
			stress_uint64_to_str(i_str, sizeof(i_str), (uint64_t)context.i_bytes, 2, true));
	}
	ret = stress_oomable_child(args, (void *)&context, stress_flushcache_child, STRESS_OOMABLE_NORMAL);

	(void)stress_munmap_anon_shared(context.i_addr, context.i_bytes);
	return ret;
}

const stressor_info_t stress_flushcache_info = {
	.stressor = stress_flushcache,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_flushcache_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without cache flush support"
};
#endif
