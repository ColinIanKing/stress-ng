/*
 * Copyright (C) 2026      Colin Ian King
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
#include "core-signal.h"

#include <math.h>

static const stress_help_t help[] = {
	{ NULL,	"fp_misc N",	 "start N workers performing miscellaneous floating point operations" },
	{ NULL,	"fp_misc-ops N", "stop after N floating point miscellaneous bogo operations" },
	{ NULL,	NULL,		 NULL }
};

static float fp_float_x;		/* random float */
static float fp_float_y;		/* must be > fp_float_x */
static float fp_float_z;		/* must be >= fp_float_y */
static float fp_float_nan;		/* not a number */
static float fp_float_inf;		/* Infinity */
static float fp_float_zero;		/* Zero */

static double fp_double_x;		/* random double */
static double fp_double_y;		/* must be > fp_double_x */
static double fp_double_z;		/* must be >= fp_double_y */
static double fp_double_nan;		/* not a number */
static double fp_double_inf;		/* Infinity */
static double fp_double_zero;		/* Zero */

static long double fp_long_double_x;	/* random double */
static long double fp_long_double_y;	/* must be > fp_double_x */
static long double fp_long_double_z;	/* must be >= fp_double_y */
static long double fp_long_double_nan;	/* not a number */
static long double fp_long_double_inf;	/* Infinity */
static long double fp_long_double_zero;	/* Zero */

typedef bool (*stress_fp_misc_func_t)(stress_args_t *args);

typedef struct stress_fp_misc_methods {
	stress_fp_misc_func_t	func;
	const char		*name;
	int			n_tests;
} stress_fp_misc_methods_t;

/*
 *  stress_fp_misc_fpclassify_str()
 *	fpclassify values to human readable strings
 */
static const char *stress_fp_misc_fpclassify_str(const int classification)
{
	switch (classification) {
	case FP_NAN:
		return "FP_NAN (not a number)";
	case FP_INFINITE:
		return "FP_INFINITE (+/- infinity)";
	case FP_ZERO:
		return "FP_ZERO (zero)";
	case FP_SUBNORMAL:
		return "FP_SUBNORMAL (subnormal)";
	case FP_NORMAL:
		return "FP_NORMAL (normal)";
	default:
		break;
	}
	return "unknown";
}

#if defined(isgreater)
/*
 *  stress_fp_misc_isgreater_float()
 *	exercise isgreater for float
 */
static bool OPTIMIZE3 stress_fp_misc_isgreater_float(stress_args_t *args)
{
	if (isgreater(fp_float_x, fp_float_y)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	if (isgreater(fp_float_x, fp_float_nan)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_nan);
		return false;
	}
	if (isgreater(fp_float_nan, fp_float_y)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (isgreater(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isgreaterequal)
/*
 *  stress_fp_misc_isgreaterequal_float()
 *	exercise isgreaterequal for float
 */
static bool OPTIMIZE3 stress_fp_misc_isgreaterequal_float(stress_args_t *args)
{
	if (isgreaterequal(fp_float_x, fp_float_z)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	if (isgreaterequal(fp_float_x, fp_float_nan)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_nan);
		return false;
	}
	if (isgreaterequal(fp_float_nan, fp_float_z)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (isgreaterequal(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isless)
/*
 *  stress_fp_misc_isless_float()
 *	exercise isless for float
 */
static bool OPTIMIZE3 stress_fp_misc_isless_float(stress_args_t *args)
{
	if (!isless(fp_float_x, fp_float_y)) {
		pr_fail("%s: isless(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	if (isless(fp_float_x, fp_float_nan)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_float_x, (double)fp_float_nan);
		return false;
	}
	if (isless(fp_float_nan, fp_float_y)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (isless(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessequal)
/*
 *  stress_fp_misc_islessequal_float()
 *	exercise islessequal for float
 */
static bool OPTIMIZE3 stress_fp_misc_islessequal_float(stress_args_t *args)
{
	if (!islessequal(fp_float_x, fp_float_z)) {
		pr_fail("%s: islessequal(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	if (islessequal(fp_float_x, fp_float_nan)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, (double)fp_float_x, (double)fp_float_nan);
		return false;
	}
	if (islessequal(fp_float_nan, fp_float_z)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (islessequal(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessgreater)
/*
 *  stress_fp_misc_islessgreater_float()
 *	exercise islessgreater for float
 */
static bool OPTIMIZE3 stress_fp_misc_islessgreater_float(stress_args_t *args)
{
	if (!islessgreater(fp_float_x, fp_float_y)) {
		pr_fail("%s: islessgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	if (!islessgreater(fp_float_y, fp_float_x)) {
		pr_fail("%s: islessgreater(%f,%f) returned false\n",
			args->name, (double)fp_float_y, (double)fp_float_x);
		return false;
	}
	if (islessgreater(fp_float_y, fp_float_nan)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, (double)fp_float_y, (double)fp_float_nan);
		return false;
	}
	if (islessgreater(fp_float_nan, fp_float_y)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (islessgreater(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isunordered)
/*
 *  stress_fp_misc_isunordered_float()
 *	exercise isunordered for float
 */
static bool OPTIMIZE3 stress_fp_misc_isunorderd_float(stress_args_t *args)
{
	if (!isunordered(fp_float_x, fp_float_nan)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, (double)fp_float_x, (double)fp_float_nan);
		return false;
	}
	if (!isunordered(fp_float_nan, fp_float_y)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_y);
		return false;
	}
	if (!isunordered(fp_float_nan, fp_float_nan)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, (double)fp_float_nan, (double)fp_float_nan);
		return false;
	}
	if (isunordered(fp_float_x, fp_float_y)) {
		pr_fail("%s: isunordered(%f,%f) returned true\n",
			args->name, (double)fp_float_x, (double)fp_float_y);
		return false;
	}
	return true;
}
#endif

#if defined(fpclassify)
/*
 *  stress_fp_misc_fpclassify_float()
 *	exercise fpclassify for float
 */
static bool OPTIMIZE3 stress_fp_misc_fpclassify_float(stress_args_t *args)
{
	int ret;

	if ((ret = fpclassify(fp_float_x)) != FP_NORMAL) {
		pr_fail("%s: fpclassify(%f) is not FP_NORMAL, got %s instead\n",
			args->name, (double)fp_float_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_float_inf)) != FP_INFINITE) {
		pr_fail("%s: fpclassify(%f) is not FP_INFINITE, got %s instead\n",
			args->name, (double)fp_float_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_float_zero)) != FP_ZERO) {
		pr_fail("%s: fpclassify(%f) is not FP_ZERO, got %s instead\n",
			args->name, (double)fp_float_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	return true;
}
#endif

#if defined(isfinite)
/*
 *  stress_fp_misc_isfinite_float()
 *	exercise isfinite for float
 */
static bool OPTIMIZE3 stress_fp_misc_isfinite_float(stress_args_t *args)
{
	if (!isfinite(fp_float_x)) {
		pr_fail("%s: isfinite(%f) returned false\n",
			args->name, (double)fp_float_x);
		return false;
	}
	if (isfinite(fp_float_inf)) {
		pr_fail("%s: isfinite(%f) returned true\n",
			args->name, (double)fp_float_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnormal)
/*
 *  stress_fp_misc_isnormal_float()
 *	exercise isnormal for float
 */
static bool OPTIMIZE3 stress_fp_misc_isnormal_float(stress_args_t *args)
{
	if (!isnormal(fp_float_x)) {
		pr_fail("%s: isnormal(%f) returned false\n",
			args->name, (double)fp_float_x);
		return false;
	}
	if (isnormal(fp_float_inf)) {
		pr_fail("%s: isnormal(%f) returned true\n",
			args->name, (double)fp_float_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnan)
/*
 *  stress_fp_misc_isnan_float()
 *	exercise isnan for float
 */
static bool OPTIMIZE3 stress_fp_misc_isnan_float(stress_args_t *args)
{
	if (!isnan(fp_float_nan)) {
		pr_fail("%s: isnan(%f) returned false\n",
			args->name, (double)fp_float_nan);
		return false;
	}
	if (isnan(fp_float_x)) {
		pr_fail("%s: isnan(%f) returned true\n",
			args->name, (double)fp_float_x);
		return false;
	}
	return true;
}
#endif

#if defined(isinf) &&	\
    !defined(__PCC__)
/*
 *  stress_fp_misc_isinf_float()
 *	exercise isinf for float
 */
static bool OPTIMIZE3 stress_fp_misc_isinf_float(stress_args_t *args)
{
	if (!isinf(fp_float_inf)) {
		pr_fail("%s: isinf(%f) returned false\n",
			args->name, (double)fp_float_inf);
		return false;
	}
	if (isinf(fp_float_x)) {
		pr_fail("%s: isnan(%f) returned true\n",
			args->name, (double)fp_float_x);
		return false;
	}
	return true;
}
#endif

#if defined(signbit)
/*
 *  stress_fp_misc_signbit_float()
 *	exercise signbit for float
 */
static bool OPTIMIZE3 stress_fp_misc_signbit_float(stress_args_t *args)
{
	if (signbit(fp_float_x)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, (double)fp_float_x);
		return false;
	}
	if (signbit(-fp_float_x) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, (double)-fp_float_x);
		return false;
	}
	if (signbit(fp_float_nan)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, (double)fp_float_nan);
		return false;
	}
	if (signbit(-fp_float_nan) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, (double)-fp_float_nan);
		return false;
	}
	if (signbit(fp_float_zero)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, (double)fp_float_nan);
		return false;
	}
	if (signbit(-fp_float_zero) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, (double)-fp_float_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isgreater)
/*
 *  stress_fp_misc_isgreater_double()
 *	exercise isgreater for double
 */
static bool OPTIMIZE3 stress_fp_misc_isgreater_double(stress_args_t *args)
{
	if (isgreater(fp_double_x, fp_double_y)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_double_x, (double)fp_double_y);
		return false;
	}
	if (isgreater(fp_double_x, fp_double_nan)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_double_x, (double)fp_double_nan);
		return false;
	}
	if (isgreater(fp_double_nan, fp_double_y)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_double_nan, (double)fp_double_y);
		return false;
	}
	if (isgreater(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: isgreater(%f,%f) returned false\n",
			args->name, (double)fp_double_nan, (double)fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isgreaterequal)
/*
 *  stress_fp_misc_isgreaterequal_double()
 *	exercise isgreaterequal for double
 */
static bool OPTIMIZE3 stress_fp_misc_isgreaterequal_double(stress_args_t *args)
{
	if (isgreaterequal(fp_double_x, fp_double_z)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_double_x, (double)fp_double_y);
		return false;
	}
	if (isgreaterequal(fp_double_x, fp_double_nan)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_double_x, (double)fp_double_nan);
		return false;
	}
	if (isgreaterequal(fp_double_nan, fp_double_z)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_double_nan, (double)fp_double_y);
		return false;
	}
	if (isgreaterequal(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: isgreaterequal(%f,%f) returned false\n",
			args->name, (double)fp_double_nan, (double)fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isless)
/*
 *  stress_fp_misc_isless_double()
 *	exercise isless for double
 */
static bool OPTIMIZE3 stress_fp_misc_isless_double(stress_args_t *args)
{
	if (!isless(fp_double_x, fp_double_y)) {
		pr_fail("%s: isless(%f,%f) returned false\n",
			args->name, (double)fp_double_x, (double)fp_double_y);
		return false;
	}
	if (isless(fp_double_x, fp_double_nan)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_double_x, (double)fp_double_nan);
		return false;
	}
	if (isless(fp_double_nan, fp_double_y)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_double_nan, (double)fp_double_y);
		return false;
	}
	if (isless(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: isless(%f,%f) returned true\n",
			args->name, (double)fp_double_nan, (double)fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessequal)
/*
 *  stress_fp_misc_islessequal_double()
 *	exercise islessequal for double
 */
static bool OPTIMIZE3 stress_fp_misc_islessequal_double(stress_args_t *args)
{
	if (!islessequal(fp_double_x, fp_double_z)) {
		pr_fail("%s: islessequal(%f,%f) returned false\n",
			args->name, fp_double_x, fp_double_y);
		return false;
	}
	if (islessequal(fp_double_x, fp_double_nan)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, fp_double_x, fp_double_nan);
		return false;
	}
	if (islessequal(fp_double_nan, fp_double_z)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, fp_double_nan, fp_double_y);
		return false;
	}
	if (islessequal(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: islessequal(%f,%f) returned true\n",
			args->name, fp_double_nan, fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessgreater)
/*
 *  stress_fp_misc_islessgreater_double
 *	exercise islessgreater for double
 */
static bool OPTIMIZE3 stress_fp_misc_islessgreater_double(stress_args_t *args)
{
	if (!islessgreater(fp_double_x, fp_double_y)) {
		pr_fail("%s: islessgreater(%f,%f) returned false\n",
			args->name, fp_double_x, fp_double_y);
		return false;
	}
	if (!islessgreater(fp_double_y, fp_double_x)) {
		pr_fail("%s: islessgreater(%f,%f) returned false\n",
			args->name, fp_double_y, fp_double_x);
		return false;
	}
	if (islessgreater(fp_double_y, fp_double_nan)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, fp_double_y, fp_double_nan);
		return false;
	}
	if (islessgreater(fp_double_nan, fp_double_y)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, fp_double_nan, fp_double_y);
		return false;
	}
	if (islessgreater(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: islessgreater(%f,%f) returned true\n",
			args->name, fp_double_nan, fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isunordered)
/*
 *  stress_fp_misc_isunordered_double()
 *	exercise isunordered for double
 */
static bool OPTIMIZE3 stress_fp_misc_isunorderd_double(stress_args_t *args)
{
	if (!isunordered(fp_double_x, fp_double_nan)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, fp_double_x, fp_double_nan);
		return false;
	}
	if (!isunordered(fp_double_nan, fp_double_y)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, fp_double_nan, fp_double_y);
		return false;
	}
	if (!isunordered(fp_double_nan, fp_double_nan)) {
		pr_fail("%s: isunordered(%f,%f) returned false\n",
			args->name, fp_double_nan, fp_double_nan);
		return false;
	}
	if (isunordered(fp_double_x, fp_double_y)) {
		pr_fail("%s: isunordered(%f,%f) returned true\n",
			args->name, fp_double_x, fp_double_y);
		return false;
	}
	return true;
}
#endif

#if defined(fpclassify)
/*
 *  stress_fp_misc_fpclassify_double()
 *	exercise fpclassify for double
 */
static bool OPTIMIZE3 stress_fp_misc_fpclassify_double(stress_args_t *args)
{
	int ret;

	if ((ret = fpclassify(fp_double_x)) != FP_NORMAL) {
		pr_fail("%s: fpclassify(%f) is not FP_NORMAL, got %s instead\n",
			args->name, fp_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_double_inf)) != FP_INFINITE) {
		pr_fail("%s: fpclassify(%f) is not FP_INFINITE, got %s instead\n",
			args->name, fp_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_double_zero)) != FP_ZERO) {
		pr_fail("%s: fpclassify(%f) is not FP_ZERO, got %s instead\n",
			args->name, fp_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	return true;
}
#endif

#if defined(isfinite)
/*
 *  stress_fp_misc_isfinite_double()
 *	exercise isfinite for double
 */
static bool OPTIMIZE3 stress_fp_misc_isfinite_double(stress_args_t *args)
{
	if (!isfinite(fp_double_x)) {
		pr_fail("%s: isfinite(%f) returned false\n",
			args->name, fp_double_x);
		return false;
	}
	if (isfinite(fp_double_inf)) {
		pr_fail("%s: isfinite(%f) returned true\n",
			args->name, fp_double_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnormal)
/*
 *  stress_fp_misc_isnormal_double()
 *	exercise isnormal for double
 */
static bool OPTIMIZE3 stress_fp_misc_isnormal_double(stress_args_t *args)
{
	if (!isnormal(fp_double_x)) {
		pr_fail("%s: isnormal(%f) returned false\n",
			args->name, fp_double_x);
		return false;
	}
	if (isnormal(fp_double_inf)) {
		pr_fail("%s: isnormal(%f) returned true\n",
			args->name, fp_double_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnan)
/*
 *  stress_fp_misc_isnan_double()
 *	exercise isnan for double
 */
static bool OPTIMIZE3 stress_fp_misc_isnan_double(stress_args_t *args)
{
	if (!isnan(fp_double_nan)) {
		pr_fail("%s: isnan(%f) returned false\n",
			args->name, fp_double_nan);
		return false;
	}
	if (isnan(fp_double_x)) {
		pr_fail("%s: isnan(%f) returned true\n",
			args->name, fp_double_x);
		return false;
	}
	return true;
}
#endif

#if defined(isinf) &&	\
    !defined(__PCC__)
/*
 *  stress_fp_misc_isinf_double()
 *	exercise isinf for double
 */
static bool OPTIMIZE3 stress_fp_misc_isinf_double(stress_args_t *args)
{
	if (!isinf(fp_double_inf)) {
		pr_fail("%s: isinf(%f) returned false\n",
			args->name, fp_double_inf);
		return false;
	}
	if (isinf(fp_double_x)) {
		pr_fail("%s: isnan(%f) returned true\n",
			args->name, fp_double_x);
		return false;
	}
	return true;
}
#endif

#if defined(signbit)
/*
 *  stress_fp_misc_signbit_double()
 *	exercise signbit for double
 */
static bool OPTIMIZE3 stress_fp_misc_signbit_double(stress_args_t *args)
{
	if (signbit(fp_double_x)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, fp_double_x);
		return false;
	}
	if (signbit(-fp_double_x) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, -fp_double_x);
		return false;
	}
	if (signbit(fp_double_nan)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, fp_double_nan);
		return false;
	}
	if (signbit(-fp_double_nan) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, -fp_double_nan);
		return false;
	}
	if (signbit(fp_double_zero)) {
		pr_fail("%s: signbit(%f) returned non-zero\n",
			args->name, fp_double_nan);
		return false;
	}
	if (signbit(-fp_double_zero) == 0) {
		pr_fail("%s: signbit(%f) returned zero\n",
			args->name, -fp_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isgreater)
/*
 *  stress_fp_misc_isgreater_long_double()
 *	exercise isgreater for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isgreater_long_double(stress_args_t *args)
{
	if (isgreater(fp_long_double_x, fp_long_double_y)) {
		pr_fail("%s: isgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	if (isgreater(fp_long_double_x, fp_long_double_nan)) {
		pr_fail("%s: isgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_nan);
		return false;
	}
	if (isgreater(fp_long_double_nan, fp_long_double_y)) {
		pr_fail("%s: isgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (isgreater(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: isgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isgreaterequal)
/*
 *  stress_fp_misc_isgreaterequal_long_double()
 *	exercise isgreaterequal for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isgreaterequal_long_double(stress_args_t *args)
{
	if (isgreaterequal(fp_long_double_x, fp_long_double_z)) {
		pr_fail("%s: isgreaterequal(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	if (isgreaterequal(fp_long_double_x, fp_long_double_nan)) {
		pr_fail("%s: isgreaterequal(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_nan);
		return false;
	}
	if (isgreaterequal(fp_long_double_nan, fp_long_double_z)) {
		pr_fail("%s: isgreaterequal(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (isgreaterequal(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: isgreaterequal(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isless)
/*
 *  stress_fp_misc_isless_long_double()
 *	exercise isless for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isless_long_double(stress_args_t *args)
{
	if (!isless(fp_long_double_x, fp_long_double_y)) {
		pr_fail("%s: isless(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	if (isless(fp_long_double_x, fp_long_double_nan)) {
		pr_fail("%s: isless(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_x, fp_long_double_nan);
		return false;
	}
	if (isless(fp_long_double_nan, fp_long_double_y)) {
		pr_fail("%s: isless(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (isless(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: isless(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessequal)
/*
 *  stress_fp_misc_islessequal_long_double()
 *	exercise islessequal for long double
 */
static bool OPTIMIZE3 stress_fp_misc_islessequal_long_double(stress_args_t *args)
{
	if (!islessequal(fp_long_double_x, fp_long_double_z)) {
		pr_fail("%s: islessequal(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	if (islessequal(fp_long_double_x, fp_long_double_nan)) {
		pr_fail("%s: islessequal(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_x, fp_long_double_nan);
		return false;
	}
	if (islessequal(fp_long_double_nan, fp_long_double_z)) {
		pr_fail("%s: islessequal(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (islessequal(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: islessequal(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(islessgreater)
/*
 *  stress_fp_misc_islessgreater_long_double
 *	exercise islessgreater for long double
 */
static bool OPTIMIZE3 stress_fp_misc_islessgreater_long_double(stress_args_t *args)
{
	if (!islessgreater(fp_long_double_x, fp_long_double_y)) {
		pr_fail("%s: islessgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	if (!islessgreater(fp_long_double_y, fp_long_double_x)) {
		pr_fail("%s: islessgreater(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_y, fp_long_double_x);
		return false;
	}
	if (islessgreater(fp_long_double_y, fp_long_double_nan)) {
		pr_fail("%s: islessgreater(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_y, fp_long_double_nan);
		return false;
	}
	if (islessgreater(fp_long_double_nan, fp_long_double_y)) {
		pr_fail("%s: islessgreater(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (islessgreater(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: islessgreater(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

#if defined(isunordered)
/*
 *  stress_fp_misc_isunordered_long_double()
 *	exercise isunordered for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isunorderd_long_double(stress_args_t *args)
{
	if (!isunordered(fp_long_double_x, fp_long_double_nan)) {
		pr_fail("%s: isunordered(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_x, fp_long_double_nan);
		return false;
	}
	if (!isunordered(fp_long_double_nan, fp_long_double_y)) {
		pr_fail("%s: isunordered(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_y);
		return false;
	}
	if (!isunordered(fp_long_double_nan, fp_long_double_nan)) {
		pr_fail("%s: isunordered(%Lf,%Lf) returned false\n",
			args->name, fp_long_double_nan, fp_long_double_nan);
		return false;
	}
	if (isunordered(fp_long_double_x, fp_long_double_y)) {
		pr_fail("%s: isunordered(%Lf,%Lf) returned true\n",
			args->name, fp_long_double_x, fp_long_double_y);
		return false;
	}
	return true;
}
#endif

#if defined(fpclassify)
/*
 *  stress_fp_misc_fpclassify_long_double()
 *	exercise fpclassify for long double
 */
static bool OPTIMIZE3 stress_fp_misc_fpclassify_long_double(stress_args_t *args)
{
	int ret;

	if ((ret = fpclassify(fp_long_double_x)) != FP_NORMAL) {
		pr_fail("%s: fpclassify(%Lf) is not FP_NORMAL, got %s instead\n",
			args->name, fp_long_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_long_double_inf)) != FP_INFINITE) {
		pr_fail("%s: fpclassify(%Lf) is not FP_INFINITE, got %s instead\n",
			args->name, fp_long_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	if ((ret = fpclassify(fp_long_double_zero)) != FP_ZERO) {
		pr_fail("%s: fpclassify(%Lf) is not FP_ZERO, got %s instead\n",
			args->name, fp_long_double_x, stress_fp_misc_fpclassify_str(ret));
		return false;
	}
	return true;
}
#endif

#if defined(isfinite)
/*
 *  stress_fp_misc_isfinite_long_double()
 *	exercise isfinite for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isfinite_long_double(stress_args_t *args)
{
	if (!isfinite(fp_long_double_x)) {
		pr_fail("%s: isfinite(%Lf) returned false\n",
			args->name, fp_long_double_x);
		return false;
	}
	if (isfinite(fp_long_double_inf)) {
		pr_fail("%s: isfinite(%Lf) returned true\n",
			args->name, fp_long_double_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnormal)
/*
 *  stress_fp_misc_isnormal_long_double()
 *	exercise isnormal for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isnormal_long_double(stress_args_t *args)
{
	if (!isnormal(fp_long_double_x)) {
		pr_fail("%s: isnormal(%Lf) returned false\n",
			args->name, fp_long_double_x);
		return false;
	}
	if (isnormal(fp_long_double_inf)) {
		pr_fail("%s: isnormal(%Lf) returned true\n",
			args->name, fp_long_double_inf);
		return false;
	}
	return true;
}
#endif

#if defined(isnan)
/*
 *  stress_fp_misc_isnan_long_double()
 *	exercise isnan for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isnan_long_double(stress_args_t *args)
{
	if (!isnan(fp_long_double_nan)) {
		pr_fail("%s: isnan(%Lf) returned false\n",
			args->name, fp_long_double_nan);
		return false;
	}
	if (isnan(fp_long_double_x)) {
		pr_fail("%s: isnan(%Lf) returned true\n",
			args->name, fp_long_double_x);
		return false;
	}
	return true;
}
#endif

#if defined(isinf) &&	\
    !defined(__PCC__)
/*
 *  stress_fp_misc_isinf_long_double()
 *	exercise isinf for long double
 */
static bool OPTIMIZE3 stress_fp_misc_isinf_long_double(stress_args_t *args)
{
	if (!isinf(fp_long_double_inf)) {
		pr_fail("%s: isinf(%Lf) returned false\n",
			args->name, fp_long_double_inf);
		return false;
	}
	if (isinf(fp_long_double_x)) {
		pr_fail("%s: isnan(%Lf) returned true\n",
			args->name, fp_long_double_x);
		return false;
	}
	return true;
}
#endif

#if defined(signbit)
/*
 *  stress_fp_misc_signbit_long_double()
 *	exercise signbit for long double
 */
static bool OPTIMIZE3 stress_fp_misc_signbit_long_double(stress_args_t *args)
{
	if (signbit(fp_long_double_x)) {
		pr_fail("%s: signbit(%Lf) returned non-zero\n",
			args->name, fp_long_double_x);
		return false;
	}
	if (signbit(-fp_long_double_x) == 0) {
		pr_fail("%s: signbit(%Lf) returned zero\n",
			args->name, -fp_long_double_x);
		return false;
	}
	if (signbit(fp_long_double_nan)) {
		pr_fail("%s: signbit(%Lf) returned non-zero\n",
			args->name, fp_long_double_nan);
		return false;
	}
	if (signbit(-fp_long_double_nan) == 0) {
		pr_fail("%s: signbit(%Lf) returned zero\n",
			args->name, -fp_long_double_nan);
		return false;
	}
	if (signbit(fp_long_double_zero)) {
		pr_fail("%s: signbit(%Lf) returned non-zero\n",
			args->name, fp_long_double_nan);
		return false;
	}
	if (signbit(-fp_long_double_zero) == 0) {
		pr_fail("%s: signbit(%Lf) returned zero\n",
			args->name, -fp_long_double_nan);
		return false;
	}
	return true;
}
#endif

static const stress_fp_misc_methods_t stress_fp_misc_methods[] = {
#if defined(isgreater)
	{ stress_fp_misc_isgreater_float,		"isgreater-float",	4 },
#endif
#if defined(isgreaterequal)
	{ stress_fp_misc_isgreaterequal_float,		"isgreaterequal-float",	4 },
#endif
#if defined(isless)
	{ stress_fp_misc_isless_float,			"isless-float",		4 },
#endif
#if defined(islessequal)
	{ stress_fp_misc_islessequal_float,		"islessequal-float",	4 },
#endif
#if defined(islessgreater)
	{ stress_fp_misc_islessgreater_float,		"islessgreater-float",	5 },
#endif
#if defined(isunordered)
	{ stress_fp_misc_isunorderd_float, 		"isunordered-float",	4 },
#endif
#if defined(fpclassify)
	{ stress_fp_misc_fpclassify_float,		"fpclassify-float",	3 },
#endif
#if defined(isfinite)
	{ stress_fp_misc_isfinite_float, 		"isfinity-float",	2 },
#endif
#if defined(isnormal)
	{ stress_fp_misc_isnormal_float,		"isnormal-float",	2 },
#endif
#if defined(isnan)
	{ stress_fp_misc_isnan_float,			"isnan-float",		2 },
#endif
#if defined(isinf) &&	\
    !defined(__PCC__)
	{ stress_fp_misc_isinf_float,			"isinf-float",		2 },
#endif
#if defined(signbit)
	{ stress_fp_misc_signbit_float,			"signbit-float",	6 },
#endif

#if defined(isgreater)
	{ stress_fp_misc_isgreater_double,		"isgreater-double",	4 },
#endif
#if defined(isgreaterequal)
	{ stress_fp_misc_isgreaterequal_double,		"isgreaterequal-double",4 },
#endif
#if defined(isless)
	{ stress_fp_misc_isless_double,			"isless-double",	4 },
#endif
#if defined(islessequal)
	{ stress_fp_misc_islessequal_double,		"islessequal-double",	4 },
#endif
#if defined(islessgreater)
	{ stress_fp_misc_islessgreater_double,		"islessgreater-double",	5 },
#endif
#if defined(isunordered)
	{ stress_fp_misc_isunorderd_double, 		"isunordered-double",	4 },
#endif
#if defined(fpclassify)
	{ stress_fp_misc_fpclassify_double,		"fpclassify-double",	3 },
#endif
#if defined(isfinite)
	{ stress_fp_misc_isfinite_double, 		"isfinity-double",	2 },
#endif
#if defined(isnormal)
	{ stress_fp_misc_isnormal_double,		"isnormal-double",	2 },
#endif
#if defined(isnan)
	{ stress_fp_misc_isnan_double,			"isnan-double",		2 },
#endif
#if defined(isinf) &&	\
    !defined(__PCC__)
	{ stress_fp_misc_isinf_double,			"isinf-double",		2 },
#endif
#if defined(signbit)
	{ stress_fp_misc_signbit_double,		"signbit-double",	6 },
#endif

#if defined(isgreater)
	{ stress_fp_misc_isgreater_long_double,		"isgreater-long-double", 4 },
#endif
#if defined(isgreaterequal)
	{ stress_fp_misc_isgreaterequal_long_double,	"isgreaterequal-long-double", 4 },
#endif
#if defined(isless)
	{ stress_fp_misc_isless_long_double,		"isless-long-double",	4 },
#endif
#if defined(islessequal)
	{ stress_fp_misc_islessequal_long_double,	"islessequal-long-double", 4 },
#endif
#if defined(islessgreater)
	{ stress_fp_misc_islessgreater_long_double,	"islessgreater-long-double", 5 },
#endif
#if defined(isunordered)
	{ stress_fp_misc_isunorderd_long_double, 	"isunordered-long-double", 4 },
#endif
#if defined(fpclassify)
	{ stress_fp_misc_fpclassify_long_double,	"fpclassify-long-double", 3 },
#endif
#if defined(isfinite)
	{ stress_fp_misc_isfinite_long_double, 		"isfinity-long-double",	2 },
#endif
#if defined(isnormal)
	{ stress_fp_misc_isnormal_long_double,		"isnormal-long-double",	2 },
#endif
#if defined(isnan)
	{ stress_fp_misc_isnan_long_double,		"isnan-long-double",	2 },
#endif
#if defined(isinf) &&	\
    !defined(__PCC__)
	{ stress_fp_misc_isinf_long_double,		"isinf-long-double",	2 },
#endif
#if defined(signbit)
	{ stress_fp_misc_signbit_long_double,		"signbit-long-double",	6 },
#endif
};

static float stess_fp_misc_rand_float(void)
{
	return (float)stress_mwc64() / (0.1f + (float)stress_mwc64());
}

static double stess_fp_misc_rand_double(void)
{
	return (double)stress_mwc64() / (0.1 + (double)stress_mwc64());
}

static long double stess_fp_misc_rand_long_double(void)
{
	return (long double)stress_mwc64() / (0.1L + (long double)stress_mwc64());
}

static int stress_fp_misc_supported(const char *name)
{
	if (SIZEOF_ARRAY(stress_fp_misc_methods) == 0) {
		pr_inf_skip("%s stressor will be skipped, "
			"no miscellaneous math macros defined\n", name);
                return -1;
	}
	return 0;
}

static int stress_fp_misc(stress_args_t *args)
{
	stress_metrics_t metrics[SIZEOF_ARRAY(stress_fp_misc_methods)];
	register size_t i;

	stress_signal_catch_sigill();

	for (i = 0; i < SIZEOF_ARRAY(metrics); i++) {
		metrics[i].duration = 0.0;
		metrics[i].count = 0.0;
	}

	fp_float_nan = NAN;
	fp_float_inf = INFINITY;
	fp_float_zero = 0.0f;

	fp_double_nan = (double)NAN;
	fp_double_inf = (double)INFINITY;
	fp_double_zero = 0.0;

	fp_long_double_nan = (long double)NAN;
	fp_long_double_inf = (long double)INFINITY;
	fp_long_double_zero = 0.0L;

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);


	do {
		double t;

		fp_float_x = stess_fp_misc_rand_float() + 1.0f;
		fp_float_y = fp_float_x * 1.1f;
		fp_float_z = fp_float_y;

		fp_double_x = stess_fp_misc_rand_double() + 1.0;
		fp_double_y = fp_double_x * 1.1;
		fp_double_z = fp_double_y;

		fp_long_double_x = stess_fp_misc_rand_long_double() + 1.0L;
		fp_long_double_y = fp_long_double_x * 1.1L;
		fp_long_double_z = fp_long_double_y;

		t = stress_time_now();
		for (i = 0; i < SIZEOF_ARRAY(stress_fp_misc_methods); i++) {
			register int j;

			for (j = 0; j < 1000; j++) {
				if (!stress_fp_misc_methods[i].func(args))
					goto fp_fail;
			}
			metrics[i].duration += stress_time_now() - t;
			metrics[i].count += 1000.0 * (double)stress_fp_misc_methods[i].n_tests;
		}
		stress_bogo_inc(args);
	} while (stress_continue(args));
fp_fail:
	for (i = 0; i < SIZEOF_ARRAY(stress_fp_misc_methods); i++) {
		char buf[64];
		const double rate = metrics[i].duration > 0.0 ?
			metrics[i].count / metrics[i].duration : 0.0;

		(void)snprintf(buf, sizeof(buf), "%s ops per sec", stress_fp_misc_methods[i].name);
		stress_metrics_set(args, buf, rate, STRESS_METRIC_HARMONIC_MEAN);
	}

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

const stressor_info_t stress_fp_misc_info = {
	.stressor = stress_fp_misc,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.verify = VERIFY_ALWAYS,
	.max_metrics_items = SIZEOF_ARRAY(stress_fp_misc_methods),
	.supported = stress_fp_misc_supported,
	.help = help
};
