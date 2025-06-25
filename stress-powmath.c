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
#include "core-pragma.h"
#include "core-put.h"
#include "core-target-clones.h"

#include <math.h>

#if defined(HAVE_COMPLEX_H)
#include <complex.h>
#endif

#define STRESS_POWMATH_LOOPS	(10000)
#define PRECISION		(1.0E-4)

typedef struct {
	const char *name;
	bool (*powmath_func)(stress_args_t *args);
} stress_powmath_method_t;

static const stress_help_t help[] = {
	{ NULL,	"powmath N",	 	"start N workers exercising power math functions" },
	{ NULL,	"powmath-ops N",	"stop after N powmath bogo power math operations" },
	{ NULL, "powmath-method M",	"select power math function to exercise" },
	{ NULL,	NULL,		 	NULL }
};

#if defined(HAVE_CBRT) ||	\
    defined(HAVE_CBRTF) ||	\
    defined(HAVE_CBRTL) ||	\
    defined(HAVE_CPOW) || 	\
    defined(HAVE_CPOWF) ||	\
    defined(HAVE_CPOWL) ||	\
    defined(HAVE_CSQRT) ||	\
    defined(HAVE_CSQRTF) ||	\
    defined(HAVE_CSQRTL) ||	\
    defined(HAVE_HYPOT) ||	\
    defined(HAVE_HYPOTF) ||	\
    defined(HAVE_HYPOTL) ||	\
    defined(HAVE_POW) ||	\
    defined(HAVE_POWF) ||	\
    defined(HAVE_POWL) ||	\
    defined(HAVE_SQRT) ||	\
    defined(HAVE_SQRTF) ||	\
    defined(HAVE_SQRTL)

#if defined(HAVE_COMPLEX_H)

#if defined(HAVE_CPOW)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cpow(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;
	register const double scale = 1.0 / (double)STRESS_POWMATH_LOOPS;
	register double di = scale;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const double complex dci = di + (di * I);

		sum += shim_cpow((double complex)(i + (i * I)), dci);
		di += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CPOWF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cpowf(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;
	register const float scale = 1.0 / (float)STRESS_POWMATH_LOOPS;
	register float fi = scale;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const float complex fci = fi + (fi * I);

		sum += (complex double)shim_cpowf((double complex)(i + (i * I)), fci);
		fi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CPOWL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cpowl(stress_args_t *args)
{
	register long double complex sum = 0.0;
	register int i;
	static long complex double result = -1.0;
	static bool first_run = true;
	register const long double scale = 1.0 / (long double)STRESS_POWMATH_LOOPS;
	register long double ldi = scale;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const long double complex ldci = ldi + (ldi * I);

		sum += shim_cpowl((long double complex)(i + (i * I)), ldci);
		ldi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CSQRT)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_csqrt(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;
	register const double scale = 1.0 / (double)STRESS_POWMATH_LOOPS;
	register double di = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const double complex dci = di + (di * I);

		sum += shim_csqrt(dci);
		di += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CSQRTF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_csqrtf(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;
	register const float scale = 1.0 / (float)STRESS_POWMATH_LOOPS;
	register float fi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const float complex fci = fi + (fi * I);

		sum += (complex double)shim_csqrtf(fci);
		fi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CSQRTL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_csqrtl(stress_args_t *args)
{
	register long double complex sum = 0.0;
	register int i;
	static long complex double result = -1.0;
	static bool first_run = true;
	register const long double scale = 1.0 / (long double)STRESS_POWMATH_LOOPS;
	register long double ldi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const long double complex ldci = ldi + (ldi * I);

		sum += shim_csqrtl(ldci);
		ldi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabsl(sum - result) > PRECISION);
}
#endif

#endif

#if defined(HAVE_CBRT)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cbrt(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const double scale = 1.0 / (double)STRESS_POWMATH_LOOPS;
	register double di = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_cbrt(di);
		di += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CBRTF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cbrtf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const float scale = 1.0 / (float)STRESS_POWMATH_LOOPS;
	register float fi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += (double)shim_cbrtf(fi);
		fi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CBRTL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_cbrtl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;
	register const long double scale = 1.0 / (long double)STRESS_POWMATH_LOOPS;
	register long double ldi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_cbrtl(ldi);
		ldi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_HYPOT)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_hypot(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const double di = (double)(i + 500);

		sum += shim_hypot((double)i, di);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_HYPOTF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_hypotf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const float fi = (float)(i + 500);

		sum += (double)shim_hypotf((float)i, fi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_HYPOTL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_hypotl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 500);

		sum += shim_hypotl((long double)i, ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_POW)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_pow(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const double scale = 1.0 / (double)STRESS_POWMATH_LOOPS;
	register double di = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_pow((double)i, di);
		di += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_POWF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_powf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const float scale = 1.0 / (float)STRESS_POWMATH_LOOPS;
	register float fi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += (double)shim_powf((float)i, fi);
		fi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_POWL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_powl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;
	register const long double scale = 1.0 / (long double)STRESS_POWMATH_LOOPS;
	register long double ldi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_powl((long double)i, ldi);
		ldi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_SQRT)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_sqrt(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const double scale = 1.0 / (double)STRESS_POWMATH_LOOPS;
	register double di = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_sqrt(di);
		di += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_SQRTF)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_sqrtf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;
	register const float scale = 1.0 / (float)STRESS_POWMATH_LOOPS;
	register float fi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += (double)shim_sqrtf(fi);
		fi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_SQRTL)
static bool OPTIMIZE3 TARGET_CLONES stress_powmath_sqrtl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;
	register const long double scale = 1.0 / (long double)STRESS_POWMATH_LOOPS;
	register long double ldi = 0.0;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_POWMATH_LOOPS; i++) {
		sum += shim_sqrtl(ldi);
		ldi += scale;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

static bool stress_powmath_all(stress_args_t *args);

static const stress_powmath_method_t stress_powmath_methods[] = {
	{ "all",	stress_powmath_all },
#if defined(HAVE_COMPLEX_H)
#if defined(HAVE_CPOW)
	{ "cpow",	stress_powmath_cpow },
#endif
#if defined(HAVE_CPOWF)
	{ "cpowf",	stress_powmath_cpowf },
#endif
#if defined(HAVE_CPOWL)
	{ "cpowl",	stress_powmath_cpowl },
#endif
#if defined(HAVE_CSQRT)
	{ "csqrt",	stress_powmath_csqrt},
#endif
#if defined(HAVE_CSQRTF)
	{ "csqrtf",	stress_powmath_csqrtf },
#endif
#if defined(HAVE_CSQRTL)
	{ "csqrtl",	stress_powmath_csqrtl },
#endif
#endif
#if defined(HAVE_CBRT)
	{ "cbrt",	stress_powmath_cbrt},
#endif
#if defined(HAVE_CBRTF)
	{ "cbrtf",	stress_powmath_cbrtf},
#endif
#if defined(HAVE_CBRTL)
	{ "cbrtl",	stress_powmath_cbrtl},
#endif
#if defined(HAVE_HYPOT)
	{ "hypot",	stress_powmath_hypot},
#endif
#if defined(HAVE_HYPOTF)
	{ "hypotf",	stress_powmath_hypotf },
#endif
#if defined(HAVE_HYPOTL)
	{ "hypotl",	stress_powmath_hypotl },
#endif
#if defined(HAVE_POW)
	{ "pow",	stress_powmath_pow },
#endif
#if defined(HAVE_POWF)
	{ "powf",	stress_powmath_powf },
#endif
#if defined(HAVE_POWL)
	{ "powl",	stress_powmath_powl },
#endif
#if defined(HAVE_SQRT)
	{ "sqrt",	stress_powmath_sqrt},
#endif
#if defined(HAVE_SQRTF)
	{ "sqrtf",	stress_powmath_sqrtf},
#endif
#if defined(HAVE_SQRTL)
	{ "sqrtl",	stress_powmath_sqrtl},
#endif
};

stress_metrics_t stress_powmath_metrics[SIZEOF_ARRAY(stress_powmath_methods)];

static bool stress_powmath_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_powmath_methods[idx].powmath_func(args);
	stress_powmath_metrics[idx].duration += (stress_time_now() - t);
	stress_powmath_metrics[idx].count += 1.0;
	if (ret) {
		if (UNLIKELY(idx != 0))
			pr_fail("powmath: %s does not match expected result\n",
				stress_powmath_methods[idx].name);
	}
	return ret;
}

static bool stress_powmath_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_powmath_methods); i++) {
		ret |= stress_powmath_exercise(args, i);
	}
	return ret;
}

/*
 * stress_powmath()
 *	stress system by various powmath function calls
 */
static int stress_powmath(stress_args_t *args)
{
	size_t i, j;
	size_t powmath_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("powmath-method", &powmath_method);

	stress_zero_metrics(stress_powmath_metrics, SIZEOF_ARRAY(stress_powmath_metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(stress_powmath_exercise(args, powmath_method))) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_powmath_metrics); i++) {
		if (stress_powmath_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_POWMATH_LOOPS *
				stress_powmath_metrics[i].count / stress_powmath_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_powmath_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const char *stress_powmath_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_powmath_methods)) ? stress_powmath_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_powmath_method, "powmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_powmath_method },
	END_OPT,
};

const stressor_info_t stress_powmath_info = {
	.stressor = stress_powmath,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const stress_opt_t opts[] = {
	{ OPT_powmath_method, "powmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

const stressor_info_t stress_powmath_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#endif
