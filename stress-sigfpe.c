// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-put.h"

#if defined(HAVE_FENV_H)
#include <fenv.h>
#else
UNEXPECTED
#endif

#if defined(HAVE_FLOAT_H)
#include <float.h>
#else
UNEXPECTED
#endif

#if defined(SA_SIGINFO) &&	\
    !defined(STRESS_ARCH_HPPA)
#define STRESS_CHECK_SIGINFO
#endif

static const stress_help_t help[] = {
	{ NULL,	"sigfpe N",	"start N workers generating floating point math faults" },
	{ NULL,	"sigfpe-ops N",	"stop after N bogo floating point math faults" },
	{ NULL,	NULL,		NULL }
};

#if !defined(__UCLIBC__) &&		\
    !defined(STRESS_ARCH_ARC64) &&	\
    defined(HAVE_FENV_H) &&     	\
    defined(HAVE_FLOAT_H)

#define SNG_INTDIV	(0x40000000)
#define SNG_FLTDIV	(0x80000000)

static sigjmp_buf jmp_env;
#if defined(STRESS_CHECK_SIGINFO)
static volatile siginfo_t siginfo;
#endif

/*
 *  stress_fpehandler()
 *	SIGFPE handler
 */
#if defined(STRESS_CHECK_SIGINFO)
static void NORETURN MLOCKED_TEXT stress_fpehandler(int num, siginfo_t *info, void *ucontext)
{
	(void)num;
	(void)ucontext;

	(void)feclearexcept(FE_ALL_EXCEPT);
	siginfo = *info;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void NORETURN MLOCKED_TEXT stress_fpehandler(int num)
{
	(void)num;
	(void)feclearexcept(FE_ALL_EXCEPT);

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

#if defined(STRESS_CHECK_SIGINFO)
/*
 *  stress_sigfpe_errstr()
 *	convert sigfpe error code to string
 */
static char *stress_sigfpe_errstr(const int err)
{
	switch (err) {
#if defined(FPE_INTDIV)
	case FPE_INTDIV:
		return "FPE_INTDEV";
#endif
#if defined(FPE_INTOVF)
	case FPE_INTOVF:
		return "FPE_INTOVF";
#endif
#if defined(FPE_FLTDIV)
	case FPE_FLTDIV:
		return "FPE_FLTDIV";
#endif
#if defined(FPE_FLTOVF)
	case FPE_FLTOVF:
		return "FPE_FLTOVF";
#endif
#if defined(FPE_FLTUND)
	case FPE_FLTUND:
		return "FPE_FLTUND";
#endif
#if defined(FPE_FLTRES)
	case FPE_FLTRES:
		return "FPE_FLTRES";
#endif
#if defined(FPE_FLTINV)
	case FPE_FLTINV:
		return "FPE_FLTINV";
#endif
#if defined(FPE_FLTSUB)
	case FPE_FLTSUB:
		return "FPE_FLTSUB";
#endif
	default:
		break;
	}
	return "FPE_UNKNOWN";
}
#endif

static void NOINLINE OPTIMIZE0 stress_int_div_by_zero(void)
{
	uint8_t k = stress_mwc8();
	uint64_t zero = stress_get_uint64_zero();

	stress_uint64_put(k / zero);
}

static void NOINLINE OPTIMIZE0 stress_float_div_by_zero(void)
{
	float k = (float)stress_mwc8();
	float zero = (float)stress_get_uint64_zero();

	stress_float_put(k / zero);
}

/*
 *  stress_sigfpe
 *	stress by generating floating point errors
 */
static int stress_sigfpe(const stress_args_t *args)
{
	struct sigaction action;
	static int i = 0;
	int ret;
#if defined(STRESS_CHECK_SIGINFO)
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
#endif

	typedef struct {
		unsigned int exception;
		int	err_code;	/* cppcheck-suppress unusedStructMember */
	} stress_fpe_err_t;

	/*
	 *  FPE errors to raise
	 */
	static const stress_fpe_err_t fpe_errs[] ALIGN64 = {
#if defined(FPE_INTDIV)
		{ SNG_INTDIV,	FPE_INTDIV },	/* can be trapped */
#else
		UNEXPECTED
#endif
#if defined(FPE_FLTDIV)
		{ SNG_FLTDIV,	FPE_FLTDIV },	/* NaN or Inf, no trap */
#else
		UNEXPECTED
#endif
#if defined(FE_DIVBYZERO) &&	\
    defined(FPE_FLTDIV)
		{ FE_DIVBYZERO,	FPE_FLTDIV },	/* Nan or Inf, no trap */
#else
		UNEXPECTED
#endif
#if defined(FE_INEXACT) &&	\
    defined(FPE_FLTRES)
		{ FE_INEXACT,	FPE_FLTRES },	/* Floating-point inexact result, no trap */
#else
		UNEXPECTED
#endif
#if defined(FE_INVALID) &&	\
    defined(FPE_FLTINV)
		{ FE_INVALID,	FPE_FLTINV },	/* Invalid floating-point operation */
#else
		UNEXPECTED
#endif
#if defined(FE_OVERFLOW) &&	\
    defined(FPE_FLTOVF)
		{ FE_OVERFLOW,	FPE_FLTOVF },	/* Floating-point overflow  */
#else
		UNEXPECTED
#endif
#if defined(FE_UNDERFLOW) &&	\
    defined(FPE_FLTUND)
		{ FE_UNDERFLOW,	FPE_FLTUND },	/* Floating-point underflow */
#else
		UNEXPECTED
#endif
	};

	(void)shim_memset(&action, 0, sizeof action);

#if defined(STRESS_CHECK_SIGINFO)
	action.sa_sigaction = stress_fpehandler;
#else
	action.sa_handler = stress_fpehandler;
#endif
	(void)sigemptyset(&action.sa_mask);
#if defined(STRESS_CHECK_SIGINFO)
	action.sa_flags = SA_SIGINFO;
#endif

	ret = sigaction(SIGFPE, &action, NULL);
	if (ret < 0) {
		pr_err("%s: sigaction SIGFPE: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	(void)alarm(0);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (;;) {
#if defined(STRESS_CHECK_SIGINFO)
		static int expected_err_code;
		int code;
#endif
		unsigned int exception;

#if defined(STRESS_CHECK_SIGINFO)
		code = fpe_errs[i].err_code;
		expected_err_code = code;
#endif
		exception = fpe_errs[i].exception;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get SIGFPE, so
		 * first check if we need to terminate
		 */
		if (UNLIKELY(!stress_continue(args)))
			break;
		if (UNLIKELY(stress_time_now() > args->time_end))
			break;

		if (ret) {
			/*
			 *  A SIGFPE occurred, check the error code
			 *  matches the expected code
			 */
			(void)feclearexcept(FE_ALL_EXCEPT);

#if defined(STRESS_CHECK_SIGINFO)
			if (verify &&
			    (siginfo.si_code >= 0) &&
			    (siginfo.si_code != expected_err_code)) {
				pr_fail("%s: got SIGFPE error %d (%s), expecting %d (%s)\n",
					args->name,
					siginfo.si_code, stress_sigfpe_errstr(siginfo.si_code),
					expected_err_code, stress_sigfpe_errstr(expected_err_code));
			}
#endif
			stress_bogo_inc(args);
		} else {
#if defined(STRESS_CHECK_SIGINFO)
			siginfo.si_code = 0;
#endif
			switch (exception) {
			case SNG_FLTDIV:
				/*
				 * This is undefined behaviour, so NaN or infinity should
				 * occur, should not generate a division by zero trap but
				 * exercise it anyhow just to cover this scenario
				 */
				stress_float_div_by_zero();
				break;
			case SNG_INTDIV:
				stress_int_div_by_zero();
				break;
			default:
				/* Raise fault otherwise */
				(void)feraiseexcept((int)exception);
				break;
			}
		}
		i++;
		if (i >= (int)SIZEOF_ARRAY(fpe_errs))
			i = 0;
	}
	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)feclearexcept(FE_ALL_EXCEPT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigfpe_info = {
	.stressor = stress_sigfpe,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else
stressor_info_t stress_sigfpe_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built using uclibc or without fenv.h or float.h"
};
#endif
