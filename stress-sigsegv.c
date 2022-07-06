/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-cpu.h"
#include "core-nt-store.h"

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile void *fault_addr;
static volatile int signo;
static volatile int code;
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigsegv N",	 "start N workers generating segmentation faults" },
	{ NULL,	"sigsegv-ops N", "stop after N bogo segmentation faults" },
	{ NULL,	NULL,		 NULL }
};

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

	fault_addr = info->si_addr;
	signo = info->si_signo;
	code = info->si_code;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void NORETURN MLOCKED_TEXT stress_segvhandler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

#if defined(STRESS_ARCH_X86) &&	\
    defined(__linux__)
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

/*
 *  stress_sigsegv
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigsegv(const stress_args_t *args)
{
	uint8_t *ptr;
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
	ptr = (uint8_t *)mmap(NULL, args->page_size, PROT_READ,
		MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (ptr == MAP_FAILED) {
		pr_inf("%s: mmap of shared read only page failed: %d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_NO_RESOURCE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (;;) {
		int ret;
		struct sigaction action;

		(void)memset(&action, 0, sizeof action);
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
		if (ret < 0) {
			pr_fail("%s: sigaction SIGSEGV: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGILL, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGILL: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}
		ret = sigaction(SIGBUS, &action, NULL);
		if (ret < 0) {
			pr_fail("%s: sigaction SIGBUS: errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			goto tidy;
		}

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we segfault, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing(args))
			break;

		if (ret) {
			/* Signal was tripped */
#if defined(SA_SIGINFO)
			if (verify && fault_addr && fault_addr != ptr) {
				pr_fail("%s: expecting fault address %p, got %p instead\n",
					args->name, fault_addr, (void *)ptr);
			}
			if (verify &&
			    (signo != -1) &&
			    (signo != SIGSEGV) &&
			    (signo != SIGILL) &&
			    (signo != SIGBUS)) {
				pr_fail("%s: expecting SIGSEGV/SIGILL/SIGBUS, got %s instead\n",
					args->name, strsignal(signo));
			}
#if defined(SEGV_ACCERR)
			if (verify && (signo == SIGBUS) && (code != SEGV_ACCERR)) {
				pr_fail("%s: expecting SIGBUS si_code SEGV_ACCERR (%d), got %d instead\n",
					args->name, SEGV_ACCERR, code);
			}
#endif
#endif
			inc_counter(args);
		} else {
#if defined(SA_SIGINFO)
			signo = -1;
			code = -1;
			fault_addr = 0;
#endif
			switch (stress_mwc8() >> 6) {
#if defined(STRESS_ARCH_X86) &&	\
    defined(__linux__)
			case 0:
				/* Trip a SIGSEGV/SIGILL/SIGBUS */
				stress_sigsegv_x86_trap();
				CASE_FALLTHROUGH;
			case 1:
				/* Illegal int $88 */
				stress_sigsegv_x86_int88();
				CASE_FALLTHROUGH;
			case 2:
				/* Privileged instruction -> SIGSEGV */
				if (has_msr)
					stress_sigsegv_rdmsr();
				CASE_FALLTHROUGH;
#if defined(HAVE_NT_STORE128) &&	\
    defined(HAVE_INT128_T)
			case 3:
				if (has_sse2)
					stress_sigsegv_misaligned128nt();
				CASE_FALLTHROUGH;
#endif
#endif
			default:
				*ptr = 0;
				break;
			}
		}
	}
	rc = EXIT_SUCCESS;
tidy:
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)ptr, args->page_size);

	return rc;

}

stressor_info_t stress_sigsegv_info = {
	.stressor = stress_sigsegv,
	.class = CLASS_INTERRUPT | CLASS_OS,
#if defined(SA_SIGINFO)
	.verify = VERIFY_OPTIONAL,
#endif
	.help = help
};
