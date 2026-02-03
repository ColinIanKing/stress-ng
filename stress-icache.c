/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2021-2026 Colin Ian King
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
#include "core-asm-ret.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-mmap.h"
#include "core-signal.h"

static const stress_help_t help[] = {
	{ NULL,	"icache N",       "start N CPU instruction cache thrashing workers" },
	{ NULL,	"icache-ops N",   "stop after N icache bogo operations" },
	{ NULL, "icache-pages N", "specify number of pages to cache thrash" },
	{ NULL,	NULL,             NULL }
};

static const stress_opt_t opts[] = {
	{ OPT_icache_pages,  "icache-pages", TYPE_ID_UINT32, 0, 1024, NULL },
	END_OPT,
};


#if (defined(STRESS_ARCH_X86) ||	\
     defined(STRESS_ARCH_ARM) ||	\
     defined(STRESS_ARCH_RISCV) ||	\
     defined(STRESS_ARCH_S390) ||	\
     defined(STRESS_ARCH_PPC) ||	\
     defined(STRESS_ARCH_PPC64)) &&	\
     defined(HAVE_MPROTECT)

static int icache_madvise_nohugepage(
	stress_args_t *args,
	void *addr,
	size_t size)
{
#if defined(MADV_NOHUGEPAGE)
	if (shim_madvise((void *)addr, size, MADV_NOHUGEPAGE) < 0) {
		/*
		 * We may get EINVAL on kernels that don't support this
		 * so don't treat that as non-fatal as this is just advisory
		 */
		if (errno != EINVAL) {
			pr_inf("%s: madvise MADV_NOHUGEPAGE failed on text "
				"page %p, errno=%d (%s)\n",
				args->name, addr, errno, strerror(errno));
			return -1;
		}
	}
#else
	(void)args;
	(void)addr;
	(void)size;
#endif
	return 0;
}

static int NOINLINE icache_mprotect(
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

static int stress_icache_func(stress_args_t *args, uint8_t *pages, const size_t pages_size)
{
	if (icache_madvise_nohugepage(args, (void *)pages, pages_size) < 0)
		return EXIT_NO_RESOURCE;
	do {
		register int j = 1024;

		while (--j) {
			register uint32_t val;
			size_t i;

			/*
			 *  Change protection to make pages modifiable.
			 *  It may be that some architectures don't
			 *  allow this, so don't bail out on an
			 *  EXIT_FAILURE; this is a not necessarily a
			 *  fault in the stressor, just an arch
			 *  resource protection issue.
			 */
			if (icache_mprotect(args, (void *)pages,
					    pages_size,
					    PROT_READ | PROT_WRITE |
					    PROT_EXEC) < 0)
				return EXIT_NO_RESOURCE;

			for (i = 0; i < pages_size; i += 64) {
				void * const addr = (void * const)(pages + i);
				volatile uint32_t *vaddr = (volatile uint32_t *)addr;

				/*
				 *  Modifying executable code on x86 will
				 *  call a I-cache reload when we execute
				 *  the modified ops.
				 */
				val = *vaddr;
				*vaddr ^= ~0;

				/*
				 * ARM CPUs need us to clear the I$ between
				 * each modification of the object code.
				 *
				 * We may need to do the same for other CPUs
				 * as the default code assumes smart x86 style
				 * I$ behaviour.
				 */
				shim_flush_icache((char *)addr, (char *)addr + 64);
				*vaddr = val;
				shim_flush_icache((char *)addr, (char *)addr + 64);
			}
			/*
			 *  Set back to a text segment READ/EXEC page
			 *  attributes, this really should not fail.
			 */
			if (icache_mprotect(args, (void *)pages, pages_size,
					    PROT_READ | PROT_EXEC) < 0)
				return EXIT_FAILURE;
			for (i = 0; i < pages_size; i += 64) {
				const stress_ret_func_t icache_func = (stress_ret_func_t)(pages + i);

				icache_func();
			}
			(void)shim_cacheflush((char *)pages, (int)pages_size, SHIM_ICACHE);
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	return EXIT_SUCCESS;
}

/*
 *  stress_icache()
 *	entry point for stress instruction cache load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
static int stress_icache(stress_args_t *args)
{
	const size_t page_size = args->page_size;
	size_t i;
	uint8_t *pages;
	int ret;
	uint32_t icache_pages = 1;
	size_t pages_size;

	(void)stress_get_setting("icache-pages", &icache_pages);
	pages_size = page_size * (size_t)icache_pages;

	stress_signal_catch_sigsegv();

	pages = (uint8_t *)stress_mmap_populate(NULL, pages_size,
			PROT_READ | PROT_WRITE | PROT_EXEC,
			MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	if (pages == MAP_FAILED) {
		pr_inf_skip("%s: could not mmap %" PRIu32 " x %zuK sized page%s, errno=%d (%s), skipping stressor\n",
			args->name, icache_pages, page_size >> 10, stress_get_memfree_str(),
			errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name((void *)pages, pages_size, "opcodes");
	for (i = 0; i < pages_size; i += 64) {
		(void)shim_memcpy((void *)(pages + i), &stress_ret_opcode.opcodes, stress_ret_opcode.len);
	}

	if (stress_instance_zero(args))
		stress_usage_bytes(args, pages_size, pages_size * args->instances);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	ret = stress_icache_func(args, pages, pages_size);
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap(pages, pages_size);
	return ret;
}

const stressor_info_t stress_icache_info = {
	.stressor = stress_icache,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help
};
#else
const stressor_info_t stress_icache_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU_CACHE,
	.supported = stress_asm_ret_supported,
	.opts = opts,
	.help = help,
	.unimplemented_reason = "built without mprotect() or userspace instruction cache flushing support"
};
#endif
