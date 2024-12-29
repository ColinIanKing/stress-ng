/*
 * Copyright (C) 2024      Colin Ian King
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
#include "core-asm-x86.h"
#include "core-asm-riscv.h"
#include "core-asm-ppc64.h"
#include "core-affinity.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-numa.h"
#include "core-put.h"

#include <sched.h>

#define N_FUNCS (SIZEOF_ARRAY(stress_cachehammer_funcs))

#if defined(STRESS_ARCH_RISCV) &&	\
    defined(HAVE_ASM_RISCV_CBO_ZERO) &&	\
    defined(__NR_riscv_hwprobe) && \
    defined(RISCV_HWPROBE_EXT_ZICBOZ)
#define HAVE_RISCV_CBO_ZERO
#endif

typedef struct {
	char *name;
	bool (*valid)(void);
	void (*hammer)(void *addr, const bool is_bad_addr);
} stress_cachehammer_func_t;

static sigjmp_buf jmp_env;
static volatile uint32_t masked_flags;

static const stress_help_t help[] = {
	{ "C N","cache N",	 	"start N CPU cache thrashing workers" },
	{ NULL,	"cache-ops N",	 	"stop after N cache bogo operations" },
	{ NULL,	NULL,			NULL }
};

static void hammer_read(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	(void)*vptr;
}

static void hammer_read64(void *addr, const bool is_bad_addr)
{
	volatile uint64_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr;
	*(vptr + 0);
	*(vptr + 1);
	*(vptr + 2);
	*(vptr + 3);
	*(vptr + 4);
	*(vptr + 5);
	*(vptr + 6);
	*(vptr + 7);
}

static void hammer_write(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	*vptr = 0;
}

static void hammer_write64(void *addr, const bool is_bad_addr)
{
	volatile uint64_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;

	vptr = (volatile uint64_t *)addr;
	*(vptr + 0) = 0;
	*(vptr + 1) = 0;
	*(vptr + 2) = 0;
	*(vptr + 3) = 0;
	*(vptr + 4) = 0;
	*(vptr + 5) = 0;
	*(vptr + 6) = 0;
	*(vptr + 7) = 0;
}

static void hammer_readwrite(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr = (volatile uint8_t *)addr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	*vptr = *vptr;
}

static void hammer_readwrite64(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	*(vptr + 0);
	*(vptr + 1) = 0;
	*(vptr + 2);
	*(vptr + 3) = 0;
	*(vptr + 4);
	*(vptr + 5) = 0;
	*(vptr + 6);
	*(vptr + 7) = 0;
}

static void hammer_writeread(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	*vptr = 0;
	(void)*vptr;
}

static void hammer_writeread64(void *addr, const bool is_bad_addr)
{
	volatile uint8_t *vptr;

	if (UNLIKELY(is_bad_addr))
		return;
	vptr = (volatile uint8_t *)addr;
	*(vptr + 0) = 0;
	*(vptr + 1);
	*(vptr + 2) = 0;
	*(vptr + 3);
	*(vptr + 4) = 0;
	*(vptr + 5);
	*(vptr + 6) = 0;
	*(vptr + 7);
}

#if defined(HAVE_RISCV_CBO_ZERO)
static bool hammer_cbo_zero_valid(void)
{
	cpu_set_t cpus;
	struct riscv_hwprobe pair;

	(void)sched_getaffinity(0, sizeof(cpu_set_t), &cpus);

	pair.key = RISCV_HWPROBE_KEY_IMA_EXT_0;
	if (syscall(__NR_riscv_hwprobe, &pair, 1, sizeof(cpu_set_t), &cpus, 0) == 0) {
		if (pair.value & RISCV_HWPROBE_EXT_ZICBOZ) {
			pair.key = RISCV_HWPROBE_KEY_ZICBOZ_BLOCK_SIZE;

			if (syscall(__NR_riscv_hwprobe, &pair, 1,
				    sizeof(cpu_set_t), &cpus, 0) == 0) {
				return true;
			}
		}
	}
	return false;
}

static void hammer_cbo_zero(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	(void)stress_asm_riscv_cbo_zero((char *)addr);
	(void)stress_asm_riscv_cbo_zero((char *)addr);
}
#endif

#if defined(HAVE_BUILTIN___CLEAR_CACHE)
static void hammer_clearcache(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	__builtin___clear_cache((void *)addr, (void *)((char *)addr + 64));
}
#endif

#if defined(HAVE_ASM_PPC64_DCBST)
static void hammer_ppc64_dcbst(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_ppc64_dcbst(addr);
	stress_asm_ppc64_dcbst(addr);
}
#endif

#if defined(HAVE_ASM_PPC64_DCBT)
static void hammer_ppc64_dcbt(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_ppc64_dcbt(addr);
	stress_asm_ppc64_dcbt(addr);
}
#endif

#if defined(HAVE_ASM_PPC64_DCBTST)
static void hammer_ppc64_dcbtst(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_ppc64_dcbtst(addr);
	stress_asm_ppc64_dcbtst(addr);
}
#endif

#if defined(HAVE_ASM_PPC64_MSYNC)
static void hammer_ppc64_msync(void *addr, const bool is_bad_addr)
{
	(void)addr;
	(void)is_bad_addr;

	stress_asm_ppc64_msync();
	stress_asm_ppc64_msync();
}
#endif

static bool hammer_valid(void)
{
	return true;
}

static void hammer_prefetch(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	shim_builtin_prefetch(addr, 0, 0);
	shim_builtin_prefetch(addr, 1, 1);
	shim_builtin_prefetch(addr, 0, 2);
	shim_builtin_prefetch(addr, 1, 3);
}

#if defined(HAVE_ASM_X86_PREFETCHNTA)
static void hammer_prefetchnta(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetchnta(addr);
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT0)
static void hammer_prefetcht0(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetcht0(addr);
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT1)
static void hammer_prefetcht1(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetcht1(addr);
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHT2)
static void hammer_prefetcht2(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetcht2(addr);
}
#endif

#if defined(HAVE_ASM_X86_CLDEMOTE)
static void hammer_cldemote(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_cldemote(addr);
	stress_asm_x86_cldemote(addr);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSH)
static void hammer_clflush(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_clflush(addr);
	stress_asm_x86_clflush(addr);
}
#endif

#if defined(HAVE_ASM_X86_CLFLUSHOPT)
static void hammer_clflushopt(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_clflushopt(addr);
	stress_asm_x86_clflushopt(addr);
}
#endif

#if defined(HAVE_ASM_X86_CLWB)
static void hammer_clwb(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_clwb(addr);
	stress_asm_x86_clwb(addr);
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHW)
static void hammer_prefetchw(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetchw(addr);
}
#endif

#if defined(HAVE_ASM_X86_PREFETCHWT1)
static void hammer_prefetchwt1(void *addr, const bool is_bad_addr)
{
	(void)is_bad_addr;
	stress_asm_x86_prefetchwt1(addr);
}
#endif

static const stress_cachehammer_func_t stress_cachehammer_funcs[] = {
#if defined(HAVE_RISCV_CBO_ZERO)
	{ "cbo_zero",	hammer_cbo_zero_valid,		hammer_cbo_zero },
#endif
#if defined(HAVE_ASM_X86_CLDEMOTE)
	{ "cldemote",	stress_cpu_x86_has_cldemote,	hammer_cldemote },
#endif
#if defined(HAVE_BUILTIN___CLEAR_CACHE)
	{ "clearcache",	hammer_valid,			hammer_clearcache },
#endif
#if defined(HAVE_ASM_X86_CLFLUSH)
	{ "clflush",	stress_cpu_x86_has_clfsh,	hammer_clflush },
#endif
#if defined(HAVE_ASM_X86_CLFLUSHOPT)
	{ "clflushopt",	stress_cpu_x86_has_clflushopt,	hammer_clflushopt },
#endif
#if defined(HAVE_ASM_X86_CLWB)
	{ "clwb",	stress_cpu_x86_has_clwb,	hammer_clwb },
#endif
#if defined(HAVE_ASM_PPC64_DCBST)
	{ "dcbst",	hammer_valid,			hammer_ppc64_dcbst },
#endif
#if defined(HAVE_ASM_PPC64_DCBT)
	{ "dcbt",	hammer_valid,			hammer_ppc64_dcbt },
#endif
#if defined(HAVE_ASM_PPC64_DCBTST)
	{ "dcbtst",	hammer_valid,			hammer_ppc64_dcbtst },
#endif
#if defined(HAVE_ASM_PPC64_MSYNC)
	{ "msync",	hammer_valid,			hammer_ppc64_msync },
#endif
	{ "prefetch", 	hammer_valid,			hammer_prefetch },
#if defined(HAVE_ASM_X86_PREFETCHNTA)
	{ "prefetchnta", stress_cpu_x86_has_sse,	hammer_prefetchnta },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT0)
	{ "prefetcht0", stress_cpu_x86_has_sse,		hammer_prefetcht0 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT1)
	{ "prefetcht1", stress_cpu_x86_has_sse,		hammer_prefetcht1 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHT2)
	{ "prefetcht2", stress_cpu_x86_has_sse,		hammer_prefetcht2 },
#endif
#if defined(HAVE_ASM_X86_PREFETCHW)
	{ "prefetchw",	stress_cpu_x86_has_sse,		hammer_prefetchw },
#endif
#if defined(HAVE_ASM_X86_PREFETCHWT1)
	{ "prefetchwt1", stress_cpu_x86_has_prefetchwt1, hammer_prefetchwt1 },
#endif
	{ "read",	hammer_valid,			hammer_read },
	{ "read64",	hammer_valid,			hammer_read64 },
	{ "read-write",	hammer_valid,			hammer_readwrite },
	{ "read-write64", hammer_valid,			hammer_readwrite64 },
	{ "write",	hammer_valid,			hammer_write },
	{ "write64",	hammer_valid,			hammer_write64 },
	{ "write-read",	hammer_valid,			hammer_writeread },
	{ "write-read64", hammer_valid,			hammer_writeread64 },
};

static bool valid[N_FUNCS];
static bool trapped[N_FUNCS];
static size_t func_index;

static void NORETURN MLOCKED_TEXT stress_cache_sighandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);         /* Ugly, bounce back */
}

/*
 *  stress_cache_hammer_flags_to_str()
 *	turn set flags into correspoding name of flags
 */
static void stress_cache_hammer_flags_to_str(
	char *buf,
	size_t buf_len,
	const bool flags[N_FUNCS])
{
	char *ptr = buf;
	size_t i;

	(void)shim_memset(buf, 0, buf_len);
	for (i = 0; i < N_FUNCS; i++) {
		if (flags[i]) {
			const char *name = stress_cachehammer_funcs[i].name;
			const size_t len = strlen(name);

			(void)shim_strscpy(ptr, " ", buf_len);
			buf_len--;
			ptr++;

			(void)shim_strscpy(ptr, name, buf_len);
			buf_len -= len;
			ptr += len;
		}
	}
	*ptr = '\0';
}

/*
 *  stress_cachehammer
 *	stress cache by psuedo-random memory read/writes and
 *	if possible change CPU affinity to try to cause
 *	poor cache behaviour
 */
static int OPTIMIZE3 stress_cachehammer(stress_args_t *args)
{
	NOCLOBBER int ret = EXIT_SUCCESS;
	uint8_t *local_buffer, *local_page;
	uint8_t *const buffer = g_shared->mem_cache.buffer;
	const size_t buffer_size = (size_t)g_shared->mem_cache.size;
	const size_t local_buffer_size = buffer_size * 4;
	const uint32_t mask = ~0x3f;
	const uint32_t page_mask = (args->page_size - 1) & ~0x3f;
	size_t i;
	NOCLOBBER size_t tries = 0;
	NOCLOBBER void *bad_addr;
	char buf[1024];

	func_index = 0;
	for (i = 0; i < SIZEOF_ARRAY(stress_cachehammer_funcs); i++) {
		valid[i] = stress_cachehammer_funcs[i].valid();
		trapped[i] = false;
	}

	if (args->instance == 0) {
		pr_dbg("%s: using cache buffer size of %zuK\n",
			args->name, buffer_size / 1024);
		stress_cache_hammer_flags_to_str(buf, sizeof(buf), valid);
		if (*buf)
			pr_inf("%s: using operations:%s\n", args->name, buf);
	}

	if (sigsetjmp(jmp_env, 1)) {
		pr_inf_skip("%s: premature SIGSEGV caught, skipping stressor\n",
			args->name);
		return EXIT_NO_RESOURCE;
	}

	if (stress_sighandler(args->name, SIGSEGV, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGBUS, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;
	if (stress_sighandler(args->name, SIGILL, stress_cache_sighandler, NULL) < 0)
		return EXIT_NO_RESOURCE;

	/*
	 *  map a page then unmap it, then we have an address
	 *  that is known to be not available. If the mapping
	 *  fails we have MAP_FAILED which too is an invalid
	 *  bad address.
	 */
	bad_addr = mmap(NULL, args->page_size, PROT_READ,
		MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (bad_addr == MAP_FAILED)
		bad_addr = buffer;	/* use something */
	else
		(void)munmap(bad_addr, args->page_size);

	local_buffer = (uint8_t *)mmap(NULL, local_buffer_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (local_buffer == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes, skipping stressor\n",
			args->name, local_buffer_size);
		return EXIT_NO_RESOURCE;
	}

	local_page = mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
		MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (local_page == MAP_FAILED) {
		pr_inf_skip("%s: cannot mmap %zu bytes, skipping stressor\n",
			args->name, args->page_size);
		ret = EXIT_NO_RESOURCE;
		goto unmap_local_buffer;
	}

	(void)shim_memset(buffer, 0, buffer_size);
	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	{
		stress_numa_mask_t *numa_mask = stress_numa_mask_alloc();

		if (numa_mask) {
			stress_numa_randomize_pages(numa_mask, local_buffer, args->page_size, local_buffer_size);
			stress_numa_randomize_pages(numa_mask, local_page, args->page_size, args->page_size);
		}
		stress_numa_mask_free(numa_mask);
	}
#endif

	(void)sigsetjmp(jmp_env, 1);
	func_index = stress_mwc32modn((uint32_t)N_FUNCS);
	while (stress_continue(args)) {
		if (valid[func_index] && !trapped[func_index]) {
			const uint16_t rnd16 = stress_mwc16();
			const size_t loops = (rnd16 >> 1) & 0x3f;
			const uint8_t which = (rnd16 & 1) | ((rnd16 == 0x0008) << 1);

			uint32_t offset;
			uint8_t *addr;

			switch (which) {
			case 0:
			default:
				offset = stress_mwc32modn((uint32_t)buffer_size);
				addr = buffer + (offset & mask);

				for (i = 0; i < loops; i++)
					stress_cachehammer_funcs[func_index].hammer(addr, false);
				break;
			case 1:
				offset = stress_mwc32modn((uint32_t)local_buffer_size);
				addr = local_buffer + (offset & mask);

				for (i = 0; i < loops; i++)
					stress_cachehammer_funcs[func_index].hammer(addr, false);
				break;
			case 2:
				offset = stress_mwc32();
				addr = bad_addr + (offset & page_mask);

				stress_cachehammer_funcs[func_index].hammer(addr, true);
				break;
			case 3:
				offset = stress_mwc32modn((uint32_t)args->page_size);
				addr = local_page + (offset & page_mask);

				for (i = 0; i < loops; i++)
					stress_cachehammer_funcs[func_index].hammer(addr, true);
				break;
			}
			tries = 0;
			stress_bogo_inc(args);
			func_index = stress_mwc32modn((uint32_t)N_FUNCS);
		} else {
			tries++;
			if (tries > N_FUNCS) {
				pr_inf("%s: terminating early, cannot invoke any valid cache operations\n",
					args->name);
				break;
			}
			func_index++;
			if (func_index >= N_FUNCS)
				func_index = 0;
		}
	}

	/*
	 *  Hit an illegal instruction? report the disabled flags
	 */
	if (args->instance == 0) {
		stress_cache_hammer_flags_to_str(buf, sizeof(buf), trapped);
		if (*buf)
			pr_inf("%s: disabled%s due to SIGBUS/SEGV/SIGILL\n", args->name, buf);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)local_page, args->page_size);
unmap_local_buffer:
	(void)munmap((void *)local_buffer, local_buffer_size);
	return ret;
}

const stressor_info_t stress_cachehammer_info = {
	.stressor = stress_cachehammer,
	.class = CLASS_CPU_CACHE,
	.verify = VERIFY_NONE,
	.help = help
};
