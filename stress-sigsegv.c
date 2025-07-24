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
#include "core-arch.h"
#include "core-asm-x86.h"
#include "core-builtin.h"
#include "core-cpu.h"
#include "core-cpu-cache.h"
#include "core-nt-store.h"
#include "core-put.h"

#include <time.h>

#if defined(HAVE_SYS_IO_H)
#include <sys/io.h>
#endif

#if defined(HAVE_SYS_PRCTL_H)
#include <sys/prctl.h>
#endif

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#define BAD_ADDR	((void *)(0x10))

static const stress_help_t help[] = {
	{ NULL,	"sigsegv N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigsegv-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_SIGLONGJMP)

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile uint8_t *fault_addr;
static volatile uint8_t *expected_addr;
static volatile int signo;
static volatile int code;
#endif

/*
 *  stress_segvhandler()
 *	SEGV handler
 */
#if defined(SA_SIGINFO)
static void NORETURN MLOCKED_TEXT stress_segvhandler(
	int num,
	siginfo_t *info,
	void *ucontext)
{
	(void)num;
	(void)ucontext;

	fault_addr = (uint8_t *)info->si_addr;
	signo = info->si_signo;
	code = info->si_code;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	stress_no_return();
}
#else
static void NORETURN MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	stress_no_return();
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(__linux__)
#define HAVE_SIGSEGV_X86_TRAP
/*
 *  stress_sigsegv_x86_trap()
 *	cause an x86 instruction trap by executing an
 *	instruction that is more than the maximum of
 *	15 bytes long.  This is achieved by many REPNE
 *	instruction prefixes before a multiply. The
 *	trap will produce a segmentation fault.
 */
static NOINLINE OPTIMIZE0 void stress_sigsegv_x86_trap(void)
{
	int a = 1, b = 2;

	 __asm__ __volatile__(
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    ".byte 0xf2\n\t"
	    "mul %1\n\t"
	    : "=r" (a)
            : "r" (b), "r" (a));
	/*
 	 *  Should not get here
	 */
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(__linux__)
#define HAVE_SIGSEGV_X86_INT88
/*
 *  stress_sigsegv_x86_int88()
 *	making an illegal int trap causes a SIGSEGV on
 *	x86 linux implementations, so exercise this
 */
static NOINLINE OPTIMIZE0 void stress_sigsegv_x86_int88(void)
{
	__asm__ __volatile__("int $88\n");
	/*
	 *  Should not get here
	 */
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(__linux__)
#define HAVE_SIGSEGV_RDMSR
static void stress_sigsegv_rdmsr(void)
{
	uint32_t ecx = 0x00000010, eax, edx;

	__asm__ __volatile__("rdmsr" : "=a" (eax), "=d" (edx) : "c" (ecx));
	/*
	 *  Should not get here
	 */
}
#endif

#if defined(STRESS_ARCH_X86) &&		\
    defined(__linux__) &&		\
    defined(HAVE_NT_STORE128) &&	\
    defined(HAVE_INT128_T)
#define HAVE_SIGSEGV_MISALIGNED128NT
static void stress_sigsegv_misaligned128nt(void)
{
	/* Misaligned non-temporal 128 bit store */

	__uint128_t buffer[2];
	__uint128_t *ptr = (__uint128_t *)((uintptr_t)buffer + 1);

	stress_nt_store128(ptr, ~(__uint128_t)0);
	/*
	 *  Should not get here
	 */
}
#endif

#if defined(STRESS_ARCH_X86) &&		\
    defined(__linux__) && 		\
    defined(HAVE_SYS_PRCTL_H) &&	\
    defined(PR_SET_TSC) &&		\
    defined(PR_TSC_SIGSEGV)
#define HAVE_SIGSEGV_READ_TSC
static void stress_sigsegv_readtsc(void)
{
	/* SEGV reading tsc when tsc is not allowed */
	if (LIKELY(prctl(PR_SET_TSC, PR_TSC_SIGSEGV, 0, 0, 0) == 0))
		(void)stress_asm_x86_rdtsc();
}

static void stress_enable_readtsc(void)
{
	(void)prctl(PR_SET_TSC, PR_TSC_ENABLE, 0, 0, 0);
}
#endif

#if defined(STRESS_ARCH_X86) &&		\
    defined(__linux__) && 		\
    defined(HAVE_SYS_IO_H) &&		\
    defined(HAVE_IOPORT)
#define HAVE_SIGSEGV_READ_IO
static void stress_sigsegv_read_io(void)
{
	/* SIGSEGV on illegal port read access */
	(void)inb(0x80);
}
#endif

#if defined(__linux__) &&	\
    defined(HAVE_SYS_AUXV_H)
#define HAVE_SIGSEGV_VDSO
static void stress_sigsegv_vdso(void)
{
	const uintptr_t vdso = (uintptr_t)getauxval(AT_SYSINFO_EHDR);

	/* No vdso, don't bother */
	if (UNLIKELY(!vdso))
		return;

#if defined(HAVE_CLOCK_GETTIME) &&	\
    (defined(STRESS_ARCH_ARM) ||	\
     defined(STRESS_ARCH_MIPS) ||	\
     defined(STRESS_ARCH_PPC64) || 	\
     defined(STRESS_ARCH_PPC) || 	\
     defined(STRESS_ARCH_RISCV) ||	\
     defined(STRESS_ARCH_S390) ||	\
     defined(STRESS_ARCH_X86))
	(void)clock_gettime(CLOCK_REALTIME, BAD_ADDR);
#endif
#if defined(STRESS_ARCH_ARM) ||		\
    defined(STRESS_ARCH_MIPS) ||	\
    defined(STRESS_ARCH_PPC64) || 	\
    defined(STRESS_ARCH_PPC) || 	\
    defined(STRESS_ARCH_RISCV) ||	\
    defined(STRESS_ARCH_S390) ||	\
    defined(STRESS_ARCH_X86)
	(void)gettimeofday(BAD_ADDR, NULL);
#endif
}
#endif

/*
 *  stress_sigsegv
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigsegv(stress_args_t *args)
{
	uint8_t *ro_ptr, *none_ptr;
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_GUARD_INSTALL)
	uint8_t *guard_ptr;
#endif
	static uint32_t mask_shift;
	static uintptr_t mask, last_mask;
	NOCLOBBER int rc = EXIT_FAILURE;
#if defined(SA_SIGINFO)
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif
#if defined(STRESS_ARCH_X86) &&		\
   defined(__linux__)
	const bool has_msr = stress_cpu_x86_has_msr();
#if defined(HAVE_NT_STORE128) &&	\
    defined(HAVE_INT128_T)
	const bool has_sse2 = stress_cpu_x86_has_sse2();
#endif
#endif

	/* Allocate read only page */
	ro_ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_READ,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (ro_ptr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte read only page%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, args->page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(ro_ptr, args->page_size, "ro-page");

	/* Allocate write only page */
	none_ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (none_ptr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte write only page%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, args->page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap((void *)ro_ptr, args->page_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(ro_ptr, args->page_size, "no-page");

#if defined(HAVE_MADVISE) &&	\
    defined(MADV_GUARD_INSTALL)
	/* Allocate guard page */
	guard_ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_READ | PROT_WRITE,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (guard_ptr == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %zu byte guard page%s, "
			"errno=%d (%s), skipping stressor\n",
			args->name, args->page_size,
			stress_get_memfree_str(), errno, strerror(errno));
		(void)munmap((void *)none_ptr, args->page_size);
		(void)munmap((void *)ro_ptr, args->page_size);
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(guard_ptr, args->page_size, "guard-page");
	if (madvise(guard_ptr, args->page_size, MADV_GUARD_INSTALL) < 0) {
		/*
		 * older kernels may not have MADV_GUARD_INSTALL, so
		 * unmap and indicate it's not usable by setting guard_ptr
		 * to NULL
		 */
		(void)munmap((void *)guard_ptr, args->page_size);
		guard_ptr = NULL;
	}
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	mask = 0;
	last_mask = 0;
	mask_shift = 0;

	for (;;) {
		int ret;
		struct sigaction action;
		uintptr_t mask_bit, masked_ptr;

		(void)shim_memset(&action, 0, sizeof action);
#if defined(SA_SIGINFO)
		action.sa_sigaction = stress_segvhandler;
#else
		action.sa_handler = stress_segvhandler;
#endif
		(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
		action.sa_flags = SA_SIGINFO;
#endif
		ret = sigaction(SIGSEGV, &action, NULL);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sigaction SIGSEGV failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGILL, &action, NULL);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sigaction SIGILL failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGBUS, &action, NULL);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sigaction SIGBUS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (ret) {
			/* Signal was tripped */
#if defined(SA_SIGINFO)
			if (UNLIKELY(verify && expected_addr && fault_addr &&
				     (fault_addr < expected_addr) &&
				     (fault_addr > (expected_addr + 8)))) {
				pr_fail("%s: expecting fault address %p, got %p instead\n",
					args->name, (volatile void *)expected_addr, fault_addr);
			}
			if (UNLIKELY((verify &&
				     (signo != -1) &&
				     (signo != SIGSEGV) &&
				     (signo != SIGILL) &&
				     (signo != SIGBUS)))) {
				pr_fail("%s: expecting SIGSEGV/SIGILL/SIGBUS, got %s instead\n",
					args->name, strsignal(signo));
			}
#if defined(BUS_OBJERR) &&	\
    defined(BUS_ADRERR)
			if (UNLIKELY((verify && (signo == SIGBUS) &&
				     (code != BUS_OBJERR) &&
				     (code != BUS_ADRERR)))) {
				pr_fail("%s: expecting SIGBUS si_code BUS_OBJERR (%d) "
					"or BUS_ADRERR (%d), got %d instead\n",
					args->name, BUS_OBJERR, BUS_ADRERR, code);
			}
#endif
#endif
			stress_bogo_inc(args);
		} else {
#if defined(SA_SIGINFO)
			signo = -1;
			code = -1;
			fault_addr = NULL;
			expected_addr = NULL;
#endif
retry:
			if (UNLIKELY(!stress_continue(args)))
				break;
			switch (stress_mwc8modn(11)) {
#if defined(HAVE_SIGSEGV_X86_TRAP)
			case 0:
				/* Trip a SIGSEGV/SIGILL/SIGBUS */
				stress_sigsegv_x86_trap();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_X86_INT88)
			case 1:
				/* Illegal int $88 */
				stress_sigsegv_x86_int88();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_RDMSR)
			case 2:
				/* Privileged instruction -> SIGSEGV */
				if (has_msr)
					stress_sigsegv_rdmsr();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_MISALIGNED128NT)
			case 3:
				/* Misaligned 128 non-temporal read */
				if (has_sse2)
					stress_sigsegv_misaligned128nt();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_READ_TSC)
			case 4:
				/* Read TSC when TSC reads are disabled */
				stress_sigsegv_readtsc();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_READ_IO)
			case 5:
				/* Illegal I/O reads */
				stress_sigsegv_read_io();
				goto retry;
#endif
#if defined(HAVE_SIGSEGV_VDSO)
			case 6:
				/* Illegal address passed to VDSO system call  */
#if defined(SA_SIGINFO)
				expected_addr = (uint8_t *)BAD_ADDR;
				stress_cpu_data_cache_flush((char *)&expected_addr, (int)sizeof(*expected_addr));
#endif
				stress_sigsegv_vdso();
				goto retry;
#endif
			case 7:
#if defined(SA_SIGINFO)
				/* Write to read-only address */
				expected_addr = (uint8_t *)ro_ptr;
				stress_cpu_data_cache_flush((char *)&expected_addr, (int)sizeof(*expected_addr));
#endif
				*ro_ptr = 0;
				goto retry;
			case 8:
				/* Read from write-only address */
#if defined(SA_SIGINFO)
				expected_addr = (uint8_t *)none_ptr;
#endif
				stress_uint8_put(*none_ptr);
				goto retry;
			case 9:
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_GUARD_INSTALL)
				/* Access to guard pages is not allowed */
				if (guard_ptr)
					*guard_ptr = 0;
#endif
				goto retry;
			case 10:
				/* Read from random address, work through all address widths */
#if defined(UINTPTR_MAX)
#if UINTMAX_WIDTH > 32
				masked_ptr = (uintptr_t)stress_mwc64();
#else
				masked_ptr = (uintptr_t)stress_mwc32();
#endif
#else
				if (sizeof(masked_ptr) > 4) {
					masked_ptr = (uintptr_t)stress_mwc64();
				} else {
					masked_ptr = (uintptr_t)stress_mwc32();
				}
#endif
				mask_bit = ((uintptr_t)1 << mask_shift);
				mask |= mask_bit;
				if (mask == last_mask) {
					mask_shift = 0;
					mask_bit = ((uintptr_t)1 << mask_shift);
					mask = mask_bit;
				}
				mask_shift++;
				masked_ptr &= mask;
				masked_ptr |= mask_bit;	/* ensure top bit always set */
				last_mask = mask;
				stress_uint8_put(*(volatile uint8_t *)masked_ptr);
				goto retry;
			default:
				goto retry;
			}
		}
	}
	rc = EXIT_SUCCESS;
tidy:
#if defined(HAVE_SIGSEGV_READ_TSC)
	stress_enable_readtsc();
#endif
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
#if defined(HAVE_MADVISE) &&	\
    defined(MADV_GUARD_INSTALL)
	if (guard_ptr)
		(void)munmap((void *)guard_ptr, args->page_size);
#endif
	(void)munmap((void *)none_ptr, args->page_size);
	(void)munmap((void *)ro_ptr, args->page_size);

	return rc;
}

const stressor_info_t stress_sigsegv_info = {
	.stressor = stress_sigsegv,
	.classifier = CLASS_SIGNAL | CLASS_OS,
#if defined(SA_SIGINFO)
	.verify = VERIFY_OPTIONAL,
#endif
	.help = help
};

#else

const stressor_info_t stress_sigsegv_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.help = help,
	.unimplemented_reason = "built without siglongjmp support"
};

#endif
