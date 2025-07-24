/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-builtin.h"

static const stress_help_t help[] = {
	{ NULL,	"sigill N",	"start N workers generating SIGILL signals" },
	{ NULL,	"sigill-ops N",	"stop after N SIGILL signals" },
	{ NULL,	NULL,		NULL }
};

#if defined(STRESS_ARCH_ARM) &&	\
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile(".inst 0x0000bfff\n");
	__asm__ __volatile(".inst 0x0000dead\n");
}
#endif

#if defined(STRESS_ARCH_ALPHA) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_HPPA) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0xff,0xff,0xff,0xff\n");
}
#endif

#if defined(STRESS_ARCH_LOONG64) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	/* Loong64 is LE */
	__asm__ __volatile__(".byte 0x3f,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_M68K) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0xff,0xff\n");
}
#endif

#if defined(STRESS_ARCH_MIPS) && \
    defined(HAVE_SIGLONGJMP)
#if defined(__MIPSEB__) || defined(__MIPSEB) || defined(_MIPSEB) || defined(MIPSEB)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x3b\n");
}
#endif
#if (defined(__MIPSEL__) || defined(__MIPSEL) || defined(_MIPSEL) || defined(MIPSEL)) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x3b,0x00,0x00,0x00\n");
}
#endif
#endif

#if defined(STRESS_ARCH_PPC64) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_PPC) &&	\
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_RISCV) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_S390) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_SH4) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_SPARC) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	__asm__ __volatile__(".byte 0x00,0x00,0x00,0x00\n");
}
#endif

#if defined(STRESS_ARCH_X86) && \
    defined(HAVE_SIGLONGJMP)
#define HAVE_ILLEGAL_OP
static void stress_illegal_op(void)
{
	/* ud2 */
	__asm__ __volatile__(".byte 0x0f, 0x0b\n");
}
#endif

#if defined(HAVE_ILLEGAL_OP) &&	\
    defined(HAVE_SIGLONGJMP) && \
    defined(SIGILL)

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile void *fault_addr;
static volatile int signo;
static volatile int code;
#endif

/*
 *  stress_sigill_handler()
 *	SIGILL handler
 */
#if defined(SA_SIGINFO)
static void NORETURN MLOCKED_TEXT stress_sigill_handler(
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
	stress_no_return();
}
#else
static void NORETURN MLOCKED_TEXT stress_sigill_handler(int signum)
{
	(void)signum;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
	stress_no_return();
}
#endif

/*
 *  stress_sigill
 *	stress by generating segmentation faults by
 *	writing to a read only page
 */
static int stress_sigill(stress_args_t *args)
{
	int ret;
#if defined(SA_SIGINFO)
        const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (;;) {
		struct sigaction action;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get a SIGILL, so
		 * first check if we need to terminate
		 */
		if (UNLIKELY(!stress_continue(args)))
			break;

		if (ret) {
			/* Signal was tripped */
#if defined(SA_SIGINFO)
			if (UNLIKELY(verify &&
				     (signo != -1) &&
				     (signo != SIGILL))) {
				pr_fail("%s: expecting SIGILL, got %s instead\n",
					args->name, strsignal(signo));
			}

			/* Just verify SIGILL signals */
			if (verify && (signo == SIGILL)) {
				switch (code) {
#if defined(ILL_ILLOPC)
				case ILL_ILLOPC:
					break;
#endif
#if defined(ILL_ILLOPN)
				case ILL_ILLOPN:
					break;
#endif
#if defined(ILL_ILLADR)
				case ILL_ILLADR:
					break;
#endif
#if defined(ILL_ILLTRP)
				case ILL_ILLTRP:
					break;
#endif
#if defined(ILL_PRVOPC)
				case ILL_PRVOPC:
					break;
#endif
#if defined(ILL_PRVREG)
				case ILL_PRVREG:
					break;
#endif
#if defined(ILL_COPROC)
				case ILL_COPROC:
					break;
#endif
#if defined(ILL_BADSTK)
				case ILL_BADSTK:
					break;
#endif
				default:
					pr_fail("%s: unexpecting SIGBUS si_code %d\n",
						args->name, code);
					break;
				}
			}
#endif
		}
		stress_bogo_inc(args);

		(void)shim_memset(&action, 0, sizeof action);
#if defined(SA_SIGINFO)
		action.sa_sigaction = stress_sigill_handler;
#else
		action.sa_handler = stress_sigill_handler;
#endif
		(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
		action.sa_flags = SA_SIGINFO;
#endif
		ret = sigaction(SIGILL, &action, NULL);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sigaction SIGILL failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		ret = sigaction(SIGBUS, &action, NULL);
		if (UNLIKELY(ret < 0)) {
			pr_fail("%s: sigaction SIGBUS failed, errno=%d (%s)\n",
				args->name, errno, strerror(errno));
			break;
		}
		stress_illegal_op();
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_sigill_info = {
	.stressor = stress_sigill,
	.classifier = CLASS_SIGNAL | CLASS_OS,
#if defined(SA_SIGINFO)
	.verify = VERIFY_OPTIONAL,
#endif
	.help = help
};

#else

const stressor_info_t stress_sigill_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_SIGNAL | CLASS_OS,
	.unimplemented_reason = "built without SIGILL support or illegal opcode function not implemented or siglongjmp not supported",
	.help = help
};

#endif
