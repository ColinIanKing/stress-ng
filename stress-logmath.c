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

#define STRESS_LOGMATH_LOOPS	(10000)
#define PRECISION		(1.0E-4)

typedef struct {
	const char *name;
	bool (*logmath_func)(stress_args_t *args);
} stress_logmath_method_t;

static const stress_help_t help[] = {
	{ NULL,	"logmath N",	 	"start N workers exercising logarithmic math functions" },
	{ NULL,	"logmath-ops N",	"stop after N logmath bogo logarithmic math operations" },
	{ NULL, "logmath-method M",	"select logarithmic math function to exercise" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_CLOG) || 	\
    defined(HAVE_CLOGF) ||	\
    defined(HAVE_CLOGL) ||	\
    defined(HAVE_LOG) ||	\
    defined(HAVE_LOGF) ||	\
    defined(HAVE_LOGL) ||	\
    defined(HAVE_LOGB) ||	\
    defined(HAVE_LOGBF) ||	\
    defined(HAVE_LOGBL) ||	\
    defined(HAVE_LOG10) ||	\
    defined(HAVE_LOG10F) ||	\
    defined(HAVE_LOG10L) ||	\
    defined(HAVE_LOG2) ||	\
    defined(HAVE_LOG2F) ||	\
    defined(HAVE_LOG2L)

#if defined(HAVE_COMPLEX_H)

#if defined(HAVE_CLOG)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_clog(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const double df = (double)(i + 1);
		register const double complex dci = df + (df * I);

		sum += shim_clog(dci);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CLOGF)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_clogf(stress_args_t *args)
{
	register complex double sum = 0.0;
	register int i;
	static complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const float fi = (float)(i + 1);
		register const float complex fci = fi + (fi * I);

		sum += (complex double)shim_clogf(fci);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_cabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_CLOGL)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_clogl(stress_args_t *args)
{
	register long double complex sum = 0.0;
	register int i;
	static long complex double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 1);
		register const long double complex ldci = ldi + (ldi * I);

		sum += shim_clogl(ldci);
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

#if defined(HAVE_LOG)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const double di = (double)(i + 1);

		sum += shim_log(di);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOGF)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_logf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const float fi = (float)(i + 1);

		sum += (double)shim_logf(fi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOGL)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_logl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 1);

		sum += shim_logl(ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOGB)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_logb(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const double di = (double)(i + 1);

		sum += shim_logb(di);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOGBF)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_logbf(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const float fi = (float)(i + 1);

		sum += (double)shim_logbf(fi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOGBL)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_logbl(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 1);

		sum += shim_logbl(ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG10)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log10(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const double ldi = (double)(i + 1);

		sum += shim_log10(ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG10F)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log10f(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const float fi = (float)(i + 1);

		sum += (double)shim_log10f(fi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG10L)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log10l(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 1);

		sum += shim_log10l(ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG2)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log2(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const double di = (double)(i + 1);

		sum += shim_log2(di);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG2F)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log2f(stress_args_t *args)
{
	register double sum = 0.0;
	register int i;
	static double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const float fi = (float)(i + 1);

		sum += (double)shim_log2f(fi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_LOG2L)
static bool OPTIMIZE3 TARGET_CLONES stress_logmath_log2l(stress_args_t *args)
{
	register long double sum = 0.0;
	register int i;
	static long double result = -1.0;
	static bool first_run = true;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_LOGMATH_LOOPS; i++) {
		register const long double ldi = (long double)(i + 1);
		sum += shim_log2f(ldi);
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

static bool stress_logmath_all(stress_args_t *args);

static const stress_logmath_method_t stress_logmath_methods[] = {
	{ "all",	stress_logmath_all },
#if defined(HAVE_COMPLEX_H)
#if defined(HAVE_CLOG)
	{ "clog",	stress_logmath_clog },
#endif
#if defined(HAVE_CLOGF)
	{ "clogf",	stress_logmath_clogf },
#endif
#if defined(HAVE_CLOGL)
	{ "clogl",	stress_logmath_clogl },
#endif
#endif
#if defined(HAVE_LOG)
	{ "log",	stress_logmath_log },
#endif
#if defined(HAVE_LOGF)
	{ "logf",	stress_logmath_logf },
#endif
#if defined(HAVE_LOGL)
	{ "logl",	stress_logmath_logl },
#endif
#if defined(HAVE_LOGB)
	{ "logb",	stress_logmath_logb },
#endif
#if defined(HAVE_LOGBF)
	{ "logbf",	stress_logmath_logbf },
#endif
#if defined(HAVE_LOGBL)
	{ "logbl",	stress_logmath_logbl },
#endif
#if defined(HAVE_LOG10)
	{ "log10",	stress_logmath_log10 },
#endif
#if defined(HAVE_LOG10F)
	{ "log10f",	stress_logmath_log10f },
#endif
#if defined(HAVE_LOG10L)
	{ "log10l",	stress_logmath_log10l },
#endif
#if defined(HAVE_LOG2)
	{ "log2",	stress_logmath_log2 },
#endif
#if defined(HAVE_LOG2F)
	{ "log2f",	stress_logmath_log2f },
#endif
#if defined(HAVE_LOG2L)
	{ "log2l",	stress_logmath_log2l },
#endif
};

stress_metrics_t stress_logmath_metrics[SIZEOF_ARRAY(stress_logmath_methods)];

static const char *stress_logmath_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_logmath_methods)) ? stress_logmath_methods[i].name : NULL;
}

static bool stress_logmath_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_logmath_methods[idx].logmath_func(args);
	stress_logmath_metrics[idx].duration += (stress_time_now() - t);
	stress_logmath_metrics[idx].count += 1.0;
	if (UNLIKELY(ret)) {
		if (idx != 0)
			pr_fail("logmath: %s does not match expected result\n",
				stress_logmath_methods[idx].name);
	}
	return ret;
}

static bool stress_logmath_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_logmath_methods); i++) {
		ret |= stress_logmath_exercise(args, i);
	}
	return ret;
}

/*
 * stress_logmath()
 *	stress system by various logmath function calls
 */
static int stress_logmath(stress_args_t *args)
{
	size_t i, j;
	size_t logmath_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("logmath-method", &logmath_method);

	stress_zero_metrics(stress_logmath_metrics, SIZEOF_ARRAY(stress_logmath_metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (UNLIKELY(stress_logmath_exercise(args, logmath_method))) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_logmath_metrics); i++) {
		if (stress_logmath_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_LOGMATH_LOOPS *
				stress_logmath_metrics[i].count / stress_logmath_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_logmath_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_logmath_method, "logmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_logmath_method },
	END_OPT,
};

const stressor_info_t stress_logmath_info = {
	.stressor = stress_logmath,
	.classifier = CLASS_CPU |  CLASS_FP |CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static void stress_logmath_method(const char *opt_name, const char *opt_arg, stress_type_id_t *type_id, void *value)
{
	*type_id = TYPE_ID_SIZE_T;
	*(size_t *)value = 0;
	(void)fprintf(stderr, "logmath stressor not implemented, %s '%s' not available\n", opt_name, opt_arg);
}

static const stress_opt_t opts[] = {
	{ OPT_logmath_method, "logmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_logmath_method },
	END_OPT,
};

const stressor_info_t stress_logmath_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#endif
