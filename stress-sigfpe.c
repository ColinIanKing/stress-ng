/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#include <math.h>
#include <float.h>
#include <fenv.h>

static sigjmp_buf jmp_env;
static siginfo_t siginfo;

/*
 *  stress_fpehandler()
 *	SIGFPE handler
 */
static void MLOCKED_TEXT stress_fpehandler(int num, siginfo_t *info, void *ucontext)
{
	(void)num;
	(void)ucontext;

	(void)feclearexcept(FE_ALL_EXCEPT);
	(void)memcpy(&siginfo, info, sizeof siginfo);

	siglongjmp(jmp_env, 1);		/* Ugly, bounce back */
}

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

/*
 *  stress_sigfpe
 *	stress by generating floating point errors
 */
static int stress_sigfpe(const args_t *args)
{
	struct sigaction action;
	static int i = 1;
	int ret;
	const uint64_t zero = uint64_zero();
	const float fp_zero = (float)zero;

	/*
	 *  FPE errors to raise
	 */
	static const int fpe_errs[] = {
#if defined(FPE_INTDIV)
		FPE_INTDIV,
#endif
#if defined(FPE_INTOVF)
		FPE_INTOVF,
#endif
#if defined(FPE_FLTDIV)
		FPE_FLTDIV,
#endif
#if defined(FPE_FLTOVF)
		FPE_FLTOVF,
#endif
#if defined(FPE_FLTUND)
		FPE_FLTUND,
#endif
#if defined(FPE_FLTRES)
		FPE_FLTRES,
#endif
#if defined(FPE_FLTINV)
		FPE_FLTINV,
#endif
#if defined(FPE_FLTSUB)
		FPE_FLTSUB,
#endif
	};

	(void)memset(&action, 0, sizeof action);
	action.sa_sigaction = stress_fpehandler;
	(void)sigemptyset(&action.sa_mask);
	action.sa_flags = SA_SIGINFO;

	ret = sigaction(SIGFPE, &action, NULL);
	if (ret < 0) {
		pr_fail("%s: sigaction SIGFPE: errno=%d (%s)\n",
			args->name, errno, strerror(errno));
		return EXIT_FAILURE;
	}

	for (;;) {
		int ret;
		int err = fpe_errs[i];

		(void)feenableexcept(FE_ALL_EXCEPT);

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
			if ((g_opt_flags & OPT_FLAGS_VERIFY) &&
			    (siginfo.si_code != err)) {
				pr_fail("%s: got SIGFPE error %d (%s), expecting %d (%s)\n",
					args->name,
					siginfo.si_code, stress_sigfpe_errstr(siginfo.si_code),
					err, stress_sigfpe_errstr(err));
			}
			inc_counter(args);
		} else {
			switch(err) {
#if defined(FPE_FLTDIV)
			case FPE_FLTDIV:
				float_put(1.0 / fp_zero);
				break;
#endif
#if defined(FPE_INTDIV)
			case FPE_INTDIV:
				uint64_put(1 / zero);
				break;
#endif
			default:
				/* Raise fault otherwise */
				feraiseexcept(err);
				break;
			}
		}
		i++;
		i %= SIZEOF_ARRAY(fpe_errs);
	}

	return EXIT_SUCCESS;
}

stressor_info_t stress_sigfpe_info = {
	.stressor = stress_sigfpe,
	.class = CLASS_INTERRUPT | CLASS_OS
};
