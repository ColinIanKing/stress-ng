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
	{ NULL,	"fp-error N",	  "start N workers exercising floating point errors" },
	{ NULL,	"fp-error-ops N", "stop after N fp-error bogo operations" },
	{ NULL,	NULL,		  NULL }
};

#if !defined(__UCLIBC__) &&	\
    defined(HAVE_FENV_H) &&	\
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

#if defined(__PCC__)
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
	const stress_args_t *args,
	const char *expr,
	const double val,
	const double val_expected,
	const bool is_nan,
	const bool is_inf,
	const int errno_expected,
	const int excepts_expected)
{
#if defined(__linux__) &&		\
    !defined(STRESS_ARCH_M68K) &&	\
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
#else
	(void)errno_expected;
	(void)excepts_expected;

	if (stress_double_same(val, val_expected, is_nan, is_inf))
		return;

	pr_fail("%s: %s return was %f (expected %f)\n",
		args->name, expr,
		val, val_expected);
#endif
}

/*
 *  stress_fp_error()
 *	stress floating point error handling
 */
static int stress_fp_error(const stress_args_t *args)
{
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		volatile double d1, d2;

#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "log(-1.0)", log(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID);
#endif

#if defined(ERANGE) && defined(FE_DIVBYZERO)
		stress_fp_clear_error();
		stress_fp_check(args, "log(0.0)", log(0.0), -HUGE_VAL,
			false, false, ERANGE, FE_DIVBYZERO);
#endif

#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(-1.0)", log2(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID);
#endif

#if defined(ERANGE) && defined(FE_DIVBYZERO)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(0.0)", log2(0.0), -HUGE_VAL,
			false, false, ERANGE, FE_DIVBYZERO);
#endif

#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "sqrt(-1.0)", sqrt(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID);
#endif

#if defined(EDOM) && defined(FE_INVALID)
		stress_fp_clear_error();
		stress_fp_check(args, "sqrt(-1.0)", sqrt(-1.0), (double)NAN,
			true, false, EDOM, FE_INVALID);
#endif

#if defined(FE_INEXACT)
		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time computation of 1.0 / M_PI
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, 1.0);
		SET_VOLATILE(d2, M_PI);
		stress_fp_check(args, "1.0 / M_PI", d1 / d2, d1 / d2,
			false, false, 0, FE_INEXACT);

		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time overflow computation
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, DBL_MAX);
		SET_VOLATILE(d2, DBL_MAX / 2.0);
		stress_fp_check(args, "DBL_MAX + DBL_MAX / 2.0",
			DBL_MAX + DBL_MAX / 2.0, (double)INFINITY,
			false, true, 0, FE_OVERFLOW | FE_INEXACT);
#endif

#if defined(ERANGE) && defined(FE_UNDERFLOW)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(-1000000.0)", exp(-1000000.0), 0.0,
			false, false, ERANGE, FE_UNDERFLOW);
#endif

#if defined(ERANGE) && defined(FE_OVERFLOW)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(DBL_MAX)", exp(DBL_MAX), HUGE_VAL,
			false, false, ERANGE, FE_OVERFLOW);
#endif

		/*
		 *  Some implementations of fegetrount return
		 *  long long unsigned int, so cast the return
		 *  to int so we can check for -1 without any
		 *  warnings.
		 */
		if ((int)fegetround() == -1)
			pr_fail("%s: fegetround() returned -1\n", args->name);
		inc_counter(args);
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

stressor_info_t stress_fp_error_info = {
	.stressor = stress_fp_error,
	.class = CLASS_CPU,
	.help = help
};
#else
stressor_info_t stress_fp_error_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU,
	.help = help
};
#endif
