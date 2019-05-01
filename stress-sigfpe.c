/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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

static const help_t help[] = {
	{ NULL,	"sigfpe N",	"start N workers generating floating point math faults" },
	{ NULL,	"sigfpe-ops N",	"stop after N bogo floating point math faults" },
	{ NULL,	NULL,		NULL }
};

#if !defined(__UCLIBC__) &&	\
    defined(HAVE_FENV_H) &&     \
    defined(HAVE_FLOAT_H) 

#define SNG_INTDIV	(0x40000000)
#define SNG_FLTDIV	(0x80000000)

static sigjmp_buf jmp_env;
#if defined(SA_SIGINFO)
static volatile siginfo_t siginfo;
#endif

/*
 *  stress_fpehandler()
 *	SIGFPE handler
 */
#if defined(SA_SIGINFO)
static void MLOCKED_TEXT stress_fpehandler(int num, siginfo_t *info, void *ucontext)
{
	(void)num;
	(void)ucontext;

	(void)feclearexcept(FE_ALL_EXCEPT);
	siginfo = *info;

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#else
static void MLOCKED_TEXT stress_fpehandler(int num)
{
	(void)num;
	(void)feclearexcept(FE_ALL_EXCEPT);

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}
#endif

#if defined(SA_SIGINFO)
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

/*
 *  stress_sigfpe
 *	stress by generating floating point errors
 */
static int stress_sigfpe(const args_t *args)
{
	struct sigaction action;
	static int i = 0;
	int ret;
	const uint64_t zero = stress_uint64_zero();

	typedef struct {
		int	exception;
		int	err_code;
	} fpe_err_t;

	/*
	 *  FPE errors to raise
	 */
	static const fpe_err_t fpe_errs[] = {
#if defined(FPE_INTDIV)
		{ SNG_INTDIV,	FPE_INTDIV },
#endif
#if defined(FPE_FLTDIV)
		{ SNG_FLTDIV,	FPE_FLTDIV },
#endif
#if defined(FE_DIVBYZERO) && defined(FPE_FLTDIV)
		{ FE_DIVBYZERO,	FPE_FLTDIV },
#endif
#if defined(FE_INEXACT) && defined(FPE_FLTRES)
		{ FE_INEXACT,	FPE_FLTRES },
#endif
#if defined(FE_INVALID) && defined(FPE_FLTINV)
		{ FE_INVALID,	FPE_FLTINV },
#endif
#if defined(FE_OVERFLOW) && defined(FPE_FLTOVF)
		{ FE_OVERFLOW,	FPE_FLTOVF },
#endif
#if defined(FE_UNDERFLOW) && defined(FPE_FLTUND)
		{ FE_UNDERFLOW,	FPE_FLTUND },
#endif
	};

	(void)memset(&action, 0, sizeof action);

#if defined(SA_SIGINFO)
	action.sa_sigaction = stress_fpehandler;
#else
	action.sa_handler = stress_fpehandler;
#endif
	(void)sigemptyset(&action.sa_mask);
#if defined(SA_SIGINFO)
	action.sa_flags = SA_SIGINFO;
#endif

	ret = sigaction(SIGFPE, &action, NULL);
	if (ret < 0) {
		pr_fail("%s: sigaction SIGFPE: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	for (;;) {
#if defined(SA_SIGINFO)
		static int expected_err_code;
		int code;
#endif
		int exception;

#if defined(SA_SIGINFO)
		code = fpe_errs[i].err_code;
		expected_err_code = code;
#endif
		exception = fpe_errs[i].exception;

		ret = sigsetjmp(jmp_env, 1);
		/*
		 * We return here if we get SIGFPE, so
		 * first check if we need to terminate
		 */
		if (!keep_stressing())
			break;

		if (ret) {
			/*
			 *  A SIGFPE occurred, check the error code
			 *  matches the expected code
			 */
			(void)feclearexcept(FE_ALL_EXCEPT);

#if defined(SA_SIGINFO)
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (siginfo.si_code >= 0) &&
			    (siginfo.si_code != expected_err_code)) {
				pr_fail("%s: got SIGFPE error %d (%s), expecting %d (%s)\n",
					args->name,
					siginfo.si_code, stress_sigfpe_errstr(siginfo.si_code),
					expected_err_code, stress_sigfpe_errstr(expected_err_code));
			}
#endif
			inc_counter(args);
		} else {
#if defined(SA_SIGINFO)
			siginfo.si_code = 0;
#endif

			switch(exception) {
			case SNG_FLTDIV:
				float_put(1.0 / (float)zero);
				break;
			case SNG_INTDIV:
				uint64_put(1 / zero);
				break;
			default:
				/* Raise fault otherwise */
				(void)feraiseexcept(exception);
				break;
			}
		}
		i++;
		i %= SIZEOF_ARRAY(fpe_errs);
	}
	(void)feclearexcept(FE_ALL_EXCEPT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigfpe_info = {
	.stressor = stress_sigfpe,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#else
stressor_info_t stress_sigfpe_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_INTERRUPT | CLASS_OS,
	.help = help
};
#endif
