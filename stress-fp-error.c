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

#include <math.h>

#if defined(HAVE_FENV_H)
#include <fenv.h>
#endif

#if defined(HAVE_FLOAT_H)
#include <float.h>
#endif

static const stress_help_t help[] = {
	{ NULL,	"fp-error N",	  "start N workers exercising floating point errors" },
	{ NULL,	"fp-error-ops N", "stop after N fp-error bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if !defined(__UCLIBC__) &&		\
    !defined(STRESS_ARCH_ARC64) && 	\
    defined(HAVE_FENV_H) &&		\
    defined(HAVE_FLOAT_H)

#define SET_VOLATILE(d, val)	\
do {				\
	d = val;		\
	(void)&d;		\
} while (0)			\


static inline void stress_fp_clear_error(void)
{
	errno = 0;
	feclearexcept(FE_ALL_EXCEPT);
}

#if defined(HAVE_COMPILER_PCC)
#define shim_isinf(x)	(!isnan(x) && isnan(x - x))
#else
#define shim_isinf(x)	isinf(x)
#endif

static inline bool stress_double_same(
	const double val,
	const double val_expected,
	const bool is_nan,
	const bool is_inf)
{
	if (is_nan && isnan(val))
		return true;
	if (is_inf && shim_isinf(val))
		return true;
	if (isnan(val) && isnan(val_expected))
		return true;
	if (shim_isinf(val) && shim_isinf(val_expected))
		return true;
	return fabs(val - val_expected) < 0.0000001;
}

static void stress_fp_check(
	stress_args_t *args,
	const char *expr,
	const double val,
	const double val_expected,
	const bool is_nan,
	const bool is_inf,
	const int errno_expected,
	const int excepts_expected,
	int *rc)
{
#if defined(__linux__) &&		\
    !defined(STRESS_ARCH_M68K) &&	\
    !defined(HAVE_COMPILER_ICC) &&	\
    !defined(HAVE_COMPILER_MUSL) &&	\
    !defined(STRESS_ARCH_ARC64) &&	\
    !defined(__SOFTFP__) &&		\
    NEED_GNUC(4,8,0)
	if (stress_double_same(val, val_expected, is_nan, is_inf) &&
	    (fetestexcept(excepts_expected) & excepts_expected) &&
	    (errno == errno_expected))
		return;

	pr_fail("%s: %s return was %f (expected %f), "
		"errno=%d (expected %d), "
		"excepts=%d (expected %d)\n",
		args->name, expr,
		val, val_expected,
		errno, errno_expected,
		fetestexcept(excepts_expected), excepts_expected);
	*rc = EXIT_FAILURE;
#else
	(void)errno_expected;
	(void)excepts_expected;

	if (stress_double_same(val, val_expected, is_nan, is_inf))
		return;

	pr_fail("%s: %s return was %f (expected %f)\n",
		args->name, expr,
		val, val_expected);
	*rc = EXIT_FAILURE;
#endif
}

/*
 *  stress_fp_error()
 *	stress floating point error handling
 */
static int stress_fp_error(stress_args_t *args)
{
	int rc = EXIT_SUCCESS;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
#if defined(FE_INEXACT)
		volatile double d1, d2;
#endif
#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "log(-1.0)", log(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID, &rc);
#endif
#if defined(ERANGE) && defined(FE_DIVBYZERO)
		stress_fp_clear_error();
		stress_fp_check(args, "log(0.0)", log(0.0), -HUGE_VAL,
			false, false, ERANGE, FE_DIVBYZERO, &rc);
#endif
#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(-1.0)", log2(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID, &rc);
#endif
#if defined(ERANGE) && defined(FE_DIVBYZERO)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(0.0)", log2(0.0), -HUGE_VAL,
			false, false, ERANGE, FE_DIVBYZERO, &rc);
#endif
#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "sqrt(-1.0)", sqrt(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID, &rc);
#endif
#if defined(FE_INEXACT)
#if !defined(STRESS_ARCH_ALPHA)
		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time computation of 1.0 / M_PI
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, 1.0);
		SET_VOLATILE(d2, M_PI);
		stress_fp_check(args, "1.0 / M_PI", d1 / d2, d1 / d2,
			false, false, 0, FE_INEXACT, &rc);
#endif
		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time overflow computation
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, DBL_MAX);
		SET_VOLATILE(d2, DBL_MAX / 2.0);
		stress_fp_check(args, "DBL_MAX + DBL_MAX / 2.0",
			d1 + d2, (double)INFINITY,
			false, true, 0, FE_OVERFLOW | FE_INEXACT, &rc);
#endif
#if defined(ERANGE) && defined(FE_UNDERFLOW)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(-1000000.0)", exp(-1000000.0), 0.0,
			false, false, ERANGE, FE_UNDERFLOW, &rc);
#endif
#if defined(ERANGE) && defined(FE_OVERFLOW)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(DBL_MAX)", exp(DBL_MAX), HUGE_VAL,
			false, false, ERANGE, FE_OVERFLOW, &rc);
#endif
		/*
		 *  Some implementations of fegetrount return
		 *  long long unsigned int, so cast the return
		 *  to int so we can check for -1 without any
		 *  warnings.
		 */
		if ((int)fegetround() == -1) {
			pr_fail("%s: fegetround() returned -1\n", args->name);
			rc = EXIT_FAILURE;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_fp_error_info = {
	.stressor = stress_fp_error,
	.classifier = CLASS_CPU | CLASS_FP,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_fp_error_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without fully functional floating point error support"
};
#endif
