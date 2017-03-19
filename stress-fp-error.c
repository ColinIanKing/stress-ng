/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

static inline bool stress_double_same(const double d1, const double d2)
{
	if (isnan(d1) && isnan(d2))
		return true;
	if (isinf(d1) && isinf(d2))
		return true;
	return (d1 - d2) < 0.0000001;
}

static void stress_fp_check(
	const args_t *args,
	const char *expr,
	const double val,
	const double val_wanted,
	const int errno_wanted,
	const int excepts_wanted)
{
	if (stress_double_same(val, val_wanted) &&
	    (fetestexcept(excepts_wanted) & excepts_wanted) &&
	    (errno == errno_wanted))
		return;

	pr_fail("%s: %s return was %f (expected %f), "
		"errno=%d (expected %d), "
		"excepts=%d (expected %d)\n",
		args->name, expr,
		val, val_wanted,
		errno, errno_wanted,
		fetestexcept(excepts_wanted), excepts_wanted);
}

/*
 *  stress_fp_error()
 *	stress floating point error handling
 */
int stress_fp_error(const args_t *args)
{
	do {
		volatile double d1, d2;

#if defined(EDOM)
		stress_fp_clear_error();
		stress_fp_check(args, "log(-1.0)", log(-1.0), NAN,
			EDOM, FE_INVALID);
#endif

#if defined(ERANGE)
		stress_fp_clear_error();
		stress_fp_check(args, "log(0.0)", log(0.0), -HUGE_VAL,
			ERANGE, FE_DIVBYZERO);
#endif

#if defined(EDOM)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(-1.0)", log2(-1.0), NAN,
			EDOM, FE_INVALID);
#endif

#if defined(ERANGE)
		stress_fp_clear_error();
		stress_fp_check(args, "log2(0.0)", log2(0.0), -HUGE_VAL,
			ERANGE, FE_DIVBYZERO);
#endif

#if defined(EDOM)
		stress_fp_clear_error();
		stress_fp_check(args, "sqrt(-1.0)", sqrt(-1.0), NAN,
			EDOM, FE_INVALID);
#endif

#if defined(EDOM)
		stress_fp_clear_error();
		stress_fp_check(args, "sqrt(-1.0)", sqrt(-1.0), NAN,
			EDOM, FE_INVALID);
#endif

		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time computation of 1.0 / M_PI
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, 1.0);
		SET_VOLATILE(d2, M_PI);
		stress_fp_check(args, "1.0 / M_PI", d1 / d2, d1 / d2,
			0, FE_INEXACT);

		/*
		 * Use volatiles to force compiler to generate code
		 * to perform run time overflow computation
		 */
		stress_fp_clear_error();
		SET_VOLATILE(d1, DBL_MAX);
		SET_VOLATILE(d2, DBL_MAX / 2.0);
		stress_fp_check(args, "DBL_MAX + DBL_MAX / 2.0",
			DBL_MAX + DBL_MAX / 2.0, INFINITY,
			0, FE_OVERFLOW | FE_INEXACT);

#if defined(ERANGE)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(-1000000.0)", exp(-1000000.0), 0.0,
			ERANGE, FE_UNDERFLOW);
#endif

#if defined(ERANGE)
		stress_fp_clear_error();
		stress_fp_check(args, "exp(DBL_MAX)", exp(DBL_MAX), HUGE_VAL,
			ERANGE, FE_OVERFLOW);
#endif

		if (fegetround() == -1)
			pr_fail("%s: fegetround() returned -1\n", args->name);
		inc_counter(args);
	} while (keep_stressing());

	return EXIT_SUCCESS;
}
