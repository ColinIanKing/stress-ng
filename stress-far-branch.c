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

static const stress_help_t help[] = {
	{ NULL,	"far-branch N",		"start N far branching workers" },
	{ NULL,	"far-branch-ops N",	"stop after N far branching bogo operations" },
	{ NULL,	NULL,			NULL }
};

#define PAGE_MULTIPLES	(8)

#if defined(__BYTE_ORDER__) &&		\
    defined(__ORDER_LITTLE_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_LITTLE_ENDIAN__
#define STRESS_ARCH_LE
#endif
#endif

#if defined(__BYTE_ORDER__) &&		\
    defined(__ORDER_BIG_ENDIAN__)
#if __BYTE_ORDER__  == __ORDER_BIG_ENDIAN__
#define STRESS_ARCH_BE
#endif
#endif

#if defined(HAVE_MPROTECT) &&					\
    ((defined(STRESS_ARCH_ARM) && defined(__aarch64___)) ||	\
     defined(STRESS_ARCH_ALPHA) ||				\
     defined(STRESS_ARCH_HPPA) ||				\
     defined(STRESS_ARCH_M68K) ||				\
     (defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_LE)) ||	\
     (defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_BE)) ||	\
     defined(STRESS_ARCH_RISCV) ||				\
     defined(STRESS_ARCH_S390) ||				\
     defined(STRESS_ARCH_SH4) ||				\
     defined(STRESS_ARCH_X86))

typedef struct {
	const size_t stride;		/* Bytes between each function */
	const size_t len;		/* Length of return function */
	const char *assembler;		/* Assembler */
	const uint8_t opcodes[];	/* Opcodes of return function */
} ret_opcode_t;

static ret_opcode_t ret_opcode =
#if defined(STRESS_ARCH_ALPHA)
        { 16, 4, "ret", { 0x01, 0x80, 0xfa, 0x6b } };
#endif
#if defined(STRESS_ARCH_ARM) && defined(__aarch64___)
	{ 8, 4, "ret", { 0xc0, 0x03, 0x5f, 0xd6 } };
#endif
#if defined(STRESS_ARCH_HPPA)
	{ 8, 4, "bv,n r0(rp)", { 0xe8, 0x40, 0xc0, 0x02 } };
#endif
#if defined(STRESS_ARCH_M68K)
	{ 8, 2, "rts", { 0x4e, 0x75 } };
#endif
#if defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_LE)
	{ 8, 4, "jr ra", { 0x08, 0x00, 0xe0, 0x03 } };
#endif
#if defined(STRESS_ARCH_MIPS) && defined(STRESS_ARCH_BE)
	{ 8, 4, "jr ra", { 0x03, 0xe0, 0x00, 0x08 } };
#endif
#if defined(STRESS_ARCG_RISCV)
	{ 8, 2, "ret", { 0x82, 0x080 } };
#endif
#if defined(STRESS_ARCH_S390)
	{ 8, 2, "br %r14", { 0x07, 0xfe } };
#endif
#if defined(STRESS_ARCH_SH4)
	{ 8, 4, "rts", { 0x0b, 0x00, 0x09, 0x00 } };	/* rts, nop */
#endif
#if defined(STRESS_ARCH_X86)
	{ 8, 1, "ret", { 0xc3 } };
#endif

typedef void (*ret_func_t)(void);

static int sigs[] = {
#if defined(SIGILL)
	SIGILL,
#endif
#if defined(SIGSEGV)
	SIGSEGV,
#endif
#if defined(SIGBUS)
	SIGBUS,
#endif
};

static void *sig_addr = NULL;
static int sig_num = -1;
static sigjmp_buf jmp_env;

static void MLOCKED_TEXT stress_sig_handler(
        int sig,
        siginfo_t *info,
        void *ucontext)
{
	(void)ucontext;

	sig_num = sig;
	sig_addr = (info) ? info->si_addr : (void *)~(uintptr_t)0;

	keep_stressing_set_flag(false);

	siglongjmp(jmp_env, 1);
}

/*
 *  stress_far_mmap()
 *	mmap and fill pages with return op codes to simulate
 *	functions that are spread around the address space
 */
static void *stress_far_mmap(
	const size_t page_size,		/* Page size */
	const uintptr_t base,		/* Base address (stress_far_branch) */
	size_t offset, 			/* Desired offset from base */
	ret_func_t *funcs,		/* Array of function pointers */
	size_t *total_funcs)		/* Total number of functions */
{
	uint8_t *ptr = MAP_FAILED;
	size_t i;

#if defined(MAP_FIXED_NOREPLACE)
	/*
	 *  Have several attempts to mmap to a chosen location,
	 *  and shift and change offset if mapping failed (e.g.
	 *  due to clash or unmapped fixed location
	 */
	for (i = 0; offset && (i < 10); i++) {
		offset += (stress_mwc8() * 4096);
		ptr = (uint8_t *)mmap((void *)(base + offset), page_size, PROT_READ | PROT_WRITE,
			MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED_NOREPLACE, -1, 0);
		if (ptr != MAP_FAILED)
			break;
		offset <<= 1;
	}
#elif defined(HAVE_MSYNC) &&	\
      defined(MAP_FIXED)
	for (i = 0; offset && (i < 10); i++) {
		void *addr;

		offset += (stress_mwc8() * 4096);
		addr = (void *)(base + offset);

		if ((msync(addr, page_size, MS_SYNC) < 0) &&
		    (errno == ENOMEM)) {
			ptr = (uint8_t *)mmap((void *)(base + offset), page_size, PROT_READ | PROT_WRITE,
				MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED, -1, 0);
			if (ptr != MAP_FAILED)
				break;
		}
		offset <<= 1;
	}
#endif
	if (ptr == MAP_FAILED) {
		ptr = (uint8_t *)mmap(NULL, page_size, PROT_READ | PROT_WRITE,
					MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
		if (ptr == MAP_FAILED)
			return NULL;
	}

	for (i = 0; i < page_size; i += ret_opcode.stride) {
		(void)memcpy((ptr + i), ret_opcode.opcodes, ret_opcode.len);
		funcs[*total_funcs] = (ret_func_t)(ptr + i);
		(*total_funcs)++;
	}

	(void)mprotect((void *)ptr, page_size, PROT_READ | PROT_EXEC);
	return ptr;
}

/*
 *  stress_far_branch()
 *	exercise a broad randomized set of branches to functions
 *	that are spread around the entire address space; try to
 *	exercise branches that are relatively far from the stressor
 */
static int stress_far_branch(const stress_args_t *args)
{
	size_t i, j, k;
	size_t bits = sizeof(void *) * 8;
	size_t n = (bits - 16);
	size_t n_pages = n * PAGE_MULTIPLES;
	const size_t page_size = args->page_size;
	const uintptr_t mask = ~((uintptr_t)page_size - 1);
	uintptr_t base;
	const size_t max_funcs = (n_pages * page_size) / ret_opcode.stride;
	double t_start, duration, rate;
	struct sigaction sa;
	int ret;
	NOCLOBBER ret_func_t *funcs = NULL;
	NOCLOBBER void **pages = NULL;
	NOCLOBBER size_t total_funcs = 0;
	NOCLOBBER double calls = 0.0;

	base = (uintptr_t)stress_far_branch & mask;

	ret = sigsetjmp(jmp_env, 1);
	if (ret) {
		const char *sig_name = stress_signal_name(sig_num);
		static bool dumped = false;

		if (dumped)
			goto cleanup;

		dumped = true;
		if (!sig_name)
			sig_name = "(unknown)";

		pr_inf("%s: caught signal %d %s at %p\n",
			args->name, sig_num, sig_name, sig_addr);
		if (sig_num == SIGILL) {
			char buffer[256], *ptr = buffer;
			uint8_t *data = (uint8_t *)sig_addr;
			size_t buflen = sizeof(buffer);
			int len;

			len = snprintf(ptr, buflen, "%s: %p:", args->name, sig_addr);
			if (len < 0)
				goto cleanup;

			buflen -= len;
			ptr += len;
			for (i = 0; i < 8; i++, data++) {
				len = snprintf(ptr, buflen, " %2.2x", *data);
				if (len < 0)
					goto cleanup;
				buflen -= len;
				ptr += len;
			}
			pr_inf("%s\n", buffer);
		}
		goto cleanup;
	}

	if (args->instance == 0)
		pr_dbg("%s: using assembler opcode '%s' as function return code\n", args->name, ret_opcode.assembler);

	(void)memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = stress_sig_handler;
#if defined(SA_SIGINFO)
	sa.sa_flags = SA_SIGINFO;
#endif
	for (i = 0; i < SIZEOF_ARRAY(sigs); i++) {
		if (sigaction(sigs[i], &sa, NULL) < 0) {
			pr_err("%s: cannot install signal handler, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			return EXIT_FAILURE;
		}
	}

	funcs = calloc(max_funcs, sizeof(*funcs));
	if (!funcs) {
		pr_inf("%s: cannot allocate %zu function "
			"pointers, skipping stressor\n",
			args->name, max_funcs);
		return EXIT_NO_RESOURCE;
	}
	pages = calloc(n_pages, sizeof(*pages));
	if (!pages) {
		pr_inf("%s: cannot allocate %zu page "
			"pointers, skipping stressor\n",
			args->name, n_pages);
		free(funcs);
		return EXIT_NO_RESOURCE;
	}

	/*
	 *  Allocate pages and populate with simple return
	 *  functions spread across each page
	 */
	for (k = 0, i = 0; i < PAGE_MULTIPLES; i++) {
		for (j = 0; j < n; j++, k++) {
			const size_t shift = 16 + j;
			size_t offset = ((uintptr_t)1 << shift) + (4 * page_size * i);

			pages[k] = stress_far_mmap(page_size, base, offset,
						funcs, &total_funcs);
		}
	}

	if (args->instance == 0)
		pr_inf("%s: %zu functions over %zu pages\n", args->name, total_funcs, n_pages);

	/*
	 *  Shuffle function pointers to get a fairly good
	 *  random spread of address ranges to branch to
	 *  on function call/returns
	 */
	for (j = 0; j < 5; j++) {
		for (i = 0; i < total_funcs; i++) {
			register ret_func_t tmp;

			k = stress_mwc32() % total_funcs;

			tmp = funcs[i];
			funcs[i] = funcs[k];
			funcs[k] = tmp;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	t_start = stress_time_now();
	do {
		for (i = 0; i < total_funcs; i++)
			funcs[i]();
		inc_counter(args);
		calls += (double)total_funcs;
	} while (keep_stressing(args));
	duration = stress_time_now() - t_start;

	rate = (duration > 0.0) ? calls / duration : 0.0;
	stress_misc_stats_set(args->misc_stats, 0, "function calls per second", rate);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

cleanup:
	if (pages) {
		for (i = 0; i < n_pages; i++) {
			if (pages[i])
				(void)munmap((void *)pages[i], page_size);
		}
		free(pages);
	}
	free(funcs);

	return EXIT_SUCCESS;
}

stressor_info_t stress_far_branch_info = {
	.stressor = stress_far_branch,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#else
stressor_info_t stress_far_branch_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU_CACHE,
	.help = help
};
#endif
