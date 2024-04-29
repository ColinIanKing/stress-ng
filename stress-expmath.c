/*
 * Copyright (C) 2024      Colin Ian King.
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

#if defined(HAVE_COMPLEX_H)
#include <complex.h>
#endif

#define STRESS_EXPMATH_LOOPS	(10000)
#define PRECISION 		(1.0E-4)

typedef struct {
	const char *name;
	bool (*expmath_func)(stress_args_t *args);
} stress_expmath_method_t;

static const stress_help_t help[] = {
	{ NULL,	"expmath N",	 	"start N workers exercising exponential math functions" },
	{ NULL,	"expmath-ops N",	"stop after N expmath bogo exponential math operations" },
	{ NULL, "expmath-method M",	"select exponential math function to exercise" },
	{ NULL,	NULL,		 	NULL }
};

#if defined(HAVE_CEXP) || 	\
    defined(HAVE_CEXPF) ||	\
    defined(HAVE_CEXPL) ||	\
    defined(HAVE_EXP) ||	\
    defined(HAVE_EXPF) ||	\
    defined(HAVE_EXPL) ||	\
    defined(HAVE_EXP10) ||	\
    defined(HAVE_EXP10F) ||	\
    defined(HAVE_EXP10L) ||	\
    defined(HAVE_EXP2) ||	\
    defined(HAVE_EXP2F) ||	\
    defined(HAVE_EXP2L)

#if defined(HAVE_COMPLEX_H)

#if defined(HAVE_CEXP)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_cexp(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const double df = (double)i / (double)STRESS_EXPMATH_LOOPS;
		register const double complex dci = df + (df * I);

		sum += shim_cexp(dci);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CEXPF)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_cexpf(stress_args_t *args)
{
	register double complex sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const float fi = (float)i / (float)STRESS_EXPMATH_LOOPS;
		register const float complex fci = fi + (fi * I);

		sum += (complex double)shim_cexpf(fci);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CEXPL)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_cexpl(stress_args_t *args)
{
	register long double complex sum = 0.0;
	register int i;
	static long complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const long double ldi = (long double)i / (long double)STRESS_EXPMATH_LOOPS;
		register const long double complex ldci = ldi + (ldi * I);

		sum += shim_cexpl(ldci);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_cabsl(sum - result) > PRECISION);
}
#endif

#endif

#if defined(HAVE_EXP)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const double di = (double)i / (double)STRESS_EXPMATH_LOOPS;

		sum += shim_exp(di);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXPF)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_expf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const float fi = (float)i / (float)STRESS_EXPMATH_LOOPS;

		sum += (double)shim_expf(fi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXPL)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_expl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const long double ldi = (long double)i / (long double)STRESS_EXPMATH_LOOPS;

		sum += shim_expl(ldi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP10)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp10(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const double di = (double)i / (double)STRESS_EXPMATH_LOOPS;

		sum += shim_exp10(di);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP10F)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp10f(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const float fi = (float)i / (float)STRESS_EXPMATH_LOOPS;

		sum += (double)shim_exp10f(fi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP10L)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp10l(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const long double ldi = (long double)i / (long double)STRESS_EXPMATH_LOOPS;

		sum += shim_exp10l(ldi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP2)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp2(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const double di = (double)i / (double)STRESS_EXPMATH_LOOPS;

		sum += shim_exp2(di);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP2F)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp2f(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const float fi = (float)i / (float)STRESS_EXPMATH_LOOPS;

		sum += (double)shim_exp2f(fi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_EXP2L)
static bool OPTIMIZE3 TARGET_CLONES stress_expmath_exp2l(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_EXPMATH_LOOPS; i++) {
		register const long double ldi = (long double)i / (long double)STRESS_EXPMATH_LOOPS;
		sum += shim_exp2f(ldi);
	}
	stress_bogo_inc(args);

	if (first_run) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

static bool stress_expmath_all(stress_args_t *args);

static const stress_expmath_method_t stress_expmath_methods[] = {
	{ "all",	stress_expmath_all },
#if defined(HAVE_COMPLEX_H)
#if defined(HAVE_CEXP)
	{ "cexp",	stress_expmath_cexp },
#endif
#if defined(HAVE_CEXPF)
	{ "cexpf",	stress_expmath_cexpf },
#endif
#if defined(HAVE_CEXPL)
	{ "cexpl",	stress_expmath_cexpl },
#endif
#endif
#if defined(HAVE_EXP)
	{ "exp",	stress_expmath_exp },
#endif
#if defined(HAVE_EXPF)
	{ "expf",	stress_expmath_expf },
#endif
#if defined(HAVE_EXPL)
	{ "expl",	stress_expmath_expl },
#endif
#if defined(HAVE_EXP10)
	{ "exp10",	stress_expmath_exp10 },
#endif
#if defined(HAVE_EXP10F)
	{ "exp10f",	stress_expmath_exp10f },
#endif
#if defined(HAVE_EXP10L)
	{ "exp10l",	stress_expmath_exp10l },
#endif
#if defined(HAVE_EXP2)
	{ "exp2",	stress_expmath_exp2 },
#endif
#if defined(HAVE_EXP2F)
	{ "exp2f",	stress_expmath_exp2f },
#endif
#if defined(HAVE_EXP2L)
	{ "exp2l",	stress_expmath_exp2l },
#endif
};

stress_metrics_t stress_expmath_metrics[SIZEOF_ARRAY(stress_expmath_methods)];

static int stress_set_expmath_method(const char *opt)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_expmath_methods); i++) {
		if (strcmp(opt, stress_expmath_methods[i].name) == 0)
			return stress_set_setting("expmath-method", TYPE_ID_SIZE_T, &i);
	}

	(void)fprintf(stderr, "expmath-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_expmath_methods); i++) {
		(void)fprintf(stderr, " %s", stress_expmath_methods[i].name);
	}
	(void)fprintf(stderr, "\n");
	return -1;
}

static bool stress_expmath_exercise(stress_args_t *args, const size_t index)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_expmath_methods[index].expmath_func(args);
	stress_expmath_metrics[index].duration += (stress_time_now() - t);
	stress_expmath_metrics[index].count += 1.0;
	if (ret) {
		if (index != 0)
			pr_fail("expmath: %s does not match expected result\n",
				stress_expmath_methods[index].name);
	}
	return ret;
}

static bool stress_expmath_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_expmath_methods); i++) {
		ret |= stress_expmath_exercise(args, i);
	}
	return ret;
}

/*
 * stress_expmath()
 *	stress system by various expmath function calls
 */
static int stress_expmath(stress_args_t *args)
{
	size_t i, j;
	size_t expmath_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("expmath-method", &expmath_method);

	for (i = 0; i < SIZEOF_ARRAY(stress_expmath_metrics); i++) {
		stress_expmath_metrics[i].duration = 0.0;
		stress_expmath_metrics[i].count = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_expmath_exercise(args, expmath_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_expmath_metrics); i++) {
		if (stress_expmath_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_EXPMATH_LOOPS *
				stress_expmath_metrics[i].count / stress_expmath_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_expmath_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_expmath_method,	stress_set_expmath_method },
	{ 0,			NULL },
};

stressor_info_t stress_expmath_info = {
	.stressor = stress_expmath,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static int stress_set_expmath_method(const char *opt)
{
	(void)opt;

	(void)fprintf(stderr, "expmath-method is not implemented\n");
	return -1;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_expmath_method,	stress_set_expmath_method },
	{ 0,			NULL },
};

stressor_info_t stress_expmath_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_COMPUTE,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#endif
