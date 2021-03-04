/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const stress_help_t help[] = {
	{ NULL,	"icache N",	"start N CPU instruction cache thrashing workers" },
	{ NULL,	"icache-ops N",	"stop after N icache bogo operations" },
	{ NULL,	NULL,		NULL }
};

#if (defined(STRESS_ARCH_X86) ||	\
     defined(STRESS_ARCH_ARM) ||	\
     defined(STRESS_ARCH_RISC_V) ||	\
     defined(STRESS_ARCH_S390) ||	\
     defined(STRESS_ARCH_PPC64)) &&	\
     defined(__GNUC__) && 		\
     NEED_GNUC(4,6,0) &&		\
     defined(HAVE_MPROTECT)

#define SIZE_1K		(1024)
#define SIZE_4K		(4 * SIZE_1K)
#define SIZE_16K	(16 * SIZE_1K)
#define SIZE_64K	(64 * SIZE_1K)

/*
 *  STRESS_ICACHE_FUNC()
 *	generates a simple function that is page aligned in its own
 *	section so we can change the code mapping and make it
 *	modifiable to force I-cache refreshes by modifying the code
 */
#define STRESS_ICACHE_FUNC(func_name, page_sz)				\
static void SECTION(stress_icache_callee) ALIGNED(page_sz)		\
func_name(void)								\
{									\
	return;								\
}									\


/*
 *  STRESS_ICACHE()
 *	macro to generate functions that stress instruction cache
 *	load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
#define STRESS_ICACHE(func_name, page_sz, icache_func)			\
static int SECTION(stress_icache_caller) ALIGNED(page_sz) 		\
func_name(const stress_args_t *args)					\
{									\
	uint8_t *addr = (uint8_t *)icache_func;				\
	const size_t ps = args->page_size;				\
	void *page_addr = (void *)((uintptr_t)addr & ~(ps - 1));	\
									\
	if (icache_madvise(args, addr, page_sz) < 0)			\
		return EXIT_NO_RESOURCE;				\
									\
	do {								\
		register uint8_t val;					\
		register int i = 1024;					\
									\
		while (--i) {						\
			volatile uint8_t *vaddr =			\
				(volatile uint8_t *)addr;		\
			/*						\
			 *  Change protection to make page modifiable.  \
			 *  It may be that some architectures don't 	\
			 *  allow this, so don't bail out on an		\
			 *  EXIT_FAILURE; this is a not necessarily a 	\
			 *  fault in the stressor, just an arch 	\
			 *  resource protection issue.			\
			 */						\
			if (mprotect((void *)page_addr, page_sz,	\
			    PROT_READ | PROT_WRITE) < 0) {		\
				pr_inf("%s: PROT_WRITE mprotect failed "\
					"on text page %p: errno=%d "	\
					"(%s)\n", args->name, vaddr, 	\
					errno, strerror(errno));	\
				return EXIT_NO_RESOURCE;		\
			}						\
			/*						\
			 *  Modifying executable code on x86 will	\
			 *  call a I-cache reload when we execute	\
			 *  the modified ops.				\
			 */						\
			val = *vaddr;					\
			*vaddr ^= ~0;					\
			/*						\
			 * ARM CPUs need us to clear the I$ between	\
			 * each modification of the object code.	\
			 *						\
			 * We may need to do the same for other CPUs	\
			 * as the default code assumes smart x86 style	\
			 * I$ behaviour.				\
			 */						\
			shim_flush_icache((char *)addr, (char *)addr + 64);\
			*vaddr = val;					\
			shim_flush_icache((char *)addr, (char *)addr + 64);\
			/*						\
			 *  Set back to a text segment READ/EXEC page	\
			 *  attributes, this really should not fail.	\
			 */						\
			if (mprotect((void *)page_addr, page_sz,	\
			    PROT_READ | PROT_EXEC) < 0) {		\
				pr_err("%s: mprotect failed: errno=%d " \
					"(%s)\n", args->name, errno,	\
					strerror(errno));		\
				return EXIT_FAILURE;			\
			}						\
			icache_func();					\
			(void)shim_cacheflush((char *)addr, page_sz, SHIM_ICACHE); \
		}							\
		inc_counter(args);					\
	} while (keep_stressing(args));					\
									\
	return EXIT_SUCCESS;						\
}

static inline int icache_madvise(const stress_args_t *args, void *addr, size_t size)
{
#if defined(MADV_NOHUGEPAGE)
	if (shim_madvise((void *)addr, size, MADV_NOHUGEPAGE) < 0) {
		/*
		 * We may get EINVAL on kernels that don't support this
		 * so don't treat that as non-fatal as this is just advistory
		 */
		if (errno != EINVAL) {
			pr_inf("%s: madvise MADV_NOHUGEPAGE failed on text "
				"page %p: errno=%d (%s)\n",
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

#if defined(HAVE_ALIGNED_64K)
STRESS_ICACHE_FUNC(stress_icache_func_64K, SIZE_64K)
#endif
STRESS_ICACHE_FUNC(stress_icache_func_16K, SIZE_16K)
STRESS_ICACHE_FUNC(stress_icache_func_4K, SIZE_4K)

#if defined(HAVE_ALIGNED_64K)
STRESS_ICACHE(stress_icache_64K, SIZE_64K, stress_icache_func_64K)
#endif
STRESS_ICACHE(stress_icache_16K, SIZE_16K, stress_icache_func_16K)
STRESS_ICACHE(stress_icache_4K, SIZE_4K, stress_icache_func_4K)

/*
 *  stress_icache()
 *	entry point for stress instruction cache load misses
 *
 *	I-cache load misses can be observed using:
 *      perf stat -e L1-icache-load-misses stress-ng --icache 0 -t 1
 */
static int stress_icache(const stress_args_t *args)
{
	int ret;

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	switch (args->page_size) {
	case SIZE_4K:
		ret = stress_icache_4K(args);
		break;
	case SIZE_16K:
		ret = stress_icache_16K(args);
		break;
#if defined(HAVE_ALIGNED_64K)
	case SIZE_64K:
		ret = stress_icache_64K(args);
		break;
#endif
	default:
#if defined(HAVE_ALIGNED_64K)
		pr_inf("%s: page size %zu is not %u or %u or %u, cannot test\n",
			args->name, args->page_size,
			SIZE_4K, SIZE_16K, SIZE_64K);
#else
		pr_inf("%s: page size %zu is not %u or %u, cannot test\n",
			args->name, args->page_size,
			SIZE_4K, SIZE_16K);
#endif
		ret = EXIT_NO_RESOURCE;
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return ret;
}

stressor_info_t stress_icache_info = {
	.stressor = stress_icache,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#else
stressor_info_t stress_icache_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#endif
