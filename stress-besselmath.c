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

#include <math.h>

#define STRESS_BESSELMATH_LOOPS	(10000)
#define PRECISION		(1.0E-4)

typedef struct {
	const char *name;
	bool (*besselmath_func)(stress_args_t *args);
} stress_besselmath_method_t;

static const stress_help_t help[] = {
	{ NULL,	"besselmath N",	 	"start N workers exercising bessel math functions" },
	{ NULL,	"besselmath-ops N",	"stop after N besselmath bogo bessel math operations" },
	{ NULL, "besselmath-method M",	"select bessel math function to exercise" },
	{ NULL,	NULL,		 	NULL }
};

#if defined(HAVE_J0) || 	\
    defined(HAVE_J1) ||		\
    defined(HAVE_JN) ||		\
    defined(HAVE_J0F) ||	\
    defined(HAVE_J1F) ||	\
    defined(HAVE_JNF) ||	\
    defined(HAVE_J0L) ||	\
    defined(HAVE_J1L) ||	\
    defined(HAVE_JNL) ||	\
    defined(HAVE_Y0) || 	\
    defined(HAVE_Y1) ||		\
    defined(HAVE_YN) ||		\
    defined(HAVE_Y0F) ||	\
    defined(HAVE_Y1F) ||	\
    defined(HAVE_YNF) ||	\
    defined(HAVE_Y0L) ||	\
    defined(HAVE_Y1L) ||	\
    defined(HAVE_YNL)

#if defined(HAVE_J0)
static bool OPTIMIZE3 stress_besselmath_j0(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_j0(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_J1)
static bool OPTIMIZE3 stress_besselmath_j1(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_j1(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_JN)
static bool OPTIMIZE3 stress_besselmath_jn(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_jn(5, di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_J0F)
static bool OPTIMIZE3 stress_besselmath_j0f(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_j0f((float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_J1F)
static bool OPTIMIZE3 stress_besselmath_j1f(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_j1f((float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_JNF)
static bool OPTIMIZE3 stress_besselmath_jnf(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_jnf(5, (float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_J0L)
static bool OPTIMIZE3 stress_besselmath_j0l(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_j0l(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_J1L)
static bool OPTIMIZE3 stress_besselmath_j1l(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_j1l(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_JNL)
static bool OPTIMIZE3 stress_besselmath_jnl(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_jnl(5, di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y0)
static bool OPTIMIZE3 stress_besselmath_y0(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_y0(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y1)
static bool OPTIMIZE3 stress_besselmath_y1(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_y1(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_YN)
static bool OPTIMIZE3 stress_besselmath_yn(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_yn(5, di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y0F)
static bool OPTIMIZE3 stress_besselmath_y0f(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_y0f((float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y1F)
static bool OPTIMIZE3 stress_besselmath_y1f(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_y1f((float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_YNF)
static bool OPTIMIZE3 stress_besselmath_ynf(stress_args_t *args)
{
	static bool first_run = true;
	static double result = -1.0;
	register double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += (double)shim_ynf(5, (float)di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabs(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y0L)
static bool OPTIMIZE3 stress_besselmath_y0l(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_y0l(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_Y1L)
static bool OPTIMIZE3 stress_besselmath_y1l(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_y1l(di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

#if defined(HAVE_YNL)
static bool OPTIMIZE3 stress_besselmath_ynl(stress_args_t *args)
{
	static bool first_run = true;
	static long double result = -1.0;
	register long double sum = 0.0, di = 0.1;
	register int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_BESSELMATH_LOOPS; i++) {
		sum += shim_ynl(5, di);
		di += 0.001;
	}
	stress_bogo_inc(args);

	if (UNLIKELY(first_run)) {
		result = sum;
		first_run = false;
	}
	return (shim_fabsl(sum - result) > PRECISION);
}
#endif

static bool stress_besselmath_all(stress_args_t *args);

static const stress_besselmath_method_t stress_besselmath_methods[] = {
	{ "all",	stress_besselmath_all },
#if defined(HAVE_J0)
	{ "j0",		stress_besselmath_j0 },
#endif
#if defined(HAVE_J1)
	{ "j1",		stress_besselmath_j1 },
#endif
#if defined(HAVE_JN)
	{ "jn",		stress_besselmath_jn },
#endif
#if defined(HAVE_J0F)
	{ "j0f",	stress_besselmath_j0f },
#endif
#if defined(HAVE_J1F)
	{ "j1f",	stress_besselmath_j1f },
#endif
#if defined(HAVE_JNF)
	{ "jnf",	stress_besselmath_jnf },
#endif
#if defined(HAVE_J0L)
	{ "j0l",	stress_besselmath_j0l },
#endif
#if defined(HAVE_J1L)
	{ "j1l",	stress_besselmath_j1l },
#endif
#if defined(HAVE_JNL)
	{ "jnl",	stress_besselmath_jnl },
#endif
#if defined(HAVE_Y0)
	{ "y0",		stress_besselmath_y0 },
#endif
#if defined(HAVE_Y1)
	{ "y1",		stress_besselmath_y1 },
#endif
#if defined(HAVE_YN)
	{ "yn",		stress_besselmath_yn },
#endif
#if defined(HAVE_Y0F)
	{ "y0f",	stress_besselmath_y0f },
#endif
#if defined(HAVE_Y1F)
	{ "y1f",	stress_besselmath_y1f },
#endif
#if defined(HAVE_YNF)
	{ "ynf",	stress_besselmath_ynf },
#endif
#if defined(HAVE_Y0L)
	{ "y0l",	stress_besselmath_y0l },
#endif
#if defined(HAVE_Y1L)
	{ "y1l",	stress_besselmath_y1l },
#endif
#if defined(HAVE_YNL)
	{ "ynl",	stress_besselmath_ynl },
#endif
};

static const char *stress_besselmath_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_besselmath_methods)) ? stress_besselmath_methods[i].name : NULL;
}

stress_metrics_t stress_besselmath_metrics[SIZEOF_ARRAY(stress_besselmath_methods)];

static bool stress_besselmath_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_besselmath_methods[idx].besselmath_func(args);
	stress_besselmath_metrics[idx].duration += (stress_time_now() - t);
	stress_besselmath_metrics[idx].count += 1.0;
	if (ret) {
		if (idx != 0)
			pr_fail("besselmath: %s does not match expected result\n",
				stress_besselmath_methods[idx].name);
	}
	return ret;
}

static bool stress_besselmath_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_besselmath_methods); i++) {
		ret |= stress_besselmath_exercise(args, i);
	}
	return ret;
}

/*
 * stress_besselmath()
 *	stress system by various besselmath function calls
 */
static int stress_besselmath(stress_args_t *args)
{
	size_t i, j;
	size_t besselmath_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("besselmath-method", &besselmath_method);

	stress_zero_metrics(stress_besselmath_metrics, SIZEOF_ARRAY(stress_besselmath_methods));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_besselmath_exercise(args, besselmath_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_besselmath_metrics); i++) {
		if (stress_besselmath_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_BESSELMATH_LOOPS *
				stress_besselmath_metrics[i].count / stress_besselmath_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_besselmath_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_besselmath_method, "besselmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_besselmath_method },
	END_OPT,
};

const stressor_info_t stress_besselmath_info = {
	.stressor = stress_besselmath,
	.classifier = CLASS_CPU | CLASS_FP |CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const stress_opt_t opts[] = {
	{ OPT_besselmath_method, "besselmath-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	END_OPT,
};

const stressor_info_t stress_besselmath_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#endif
