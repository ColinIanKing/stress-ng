/*
 * Copyright (C) 2025      Colin Ian King.
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

#define STRESS_CTRIG_LOOPS	(10000)

typedef struct {
	const char *name;
	bool (*trig_func)(stress_args_t *args);
} stress_ctrig_method_t;

static const stress_help_t help[] = {
	{ NULL,	"ctrig N",	 "start N workers exercising complex trigonometric functions" },
	{ NULL,	"ctrig-ops N",	 "stop after N trig bogo complex trigonometric operations" },
	{ NULL, "ctrig-method M", "select complex trigonometric function to exercise" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_COMPLEX_H)

#define CCOSD_SUM (9989.57840184182168741245 + I *  421.84459305523512284708)
#define CCOSF_SUM (9989.57840049266815185547 + I *  421.84459273131869849749)
#define CCOSL_SUM (9989.57840184171815867131 + I *  421.84459305520919505939)

#define CSIND_SUM (-103.79703901230311657855 + I * 2446.84911650352341894177)
#define CSINF_SUM (-103.79704056875198148191 + I * 2446.84911444940007640980)
#define CSINL_SUM (-103.79703901192314422636 + I * 2446.84911650326313248272)

#define CTAND_SUM ( 218.42756568810500539257 + I * 2582.61959103427079753601)
#define CTANF_SUM ( 218.42756647889473242685 + I * 2582.61959293121617520228)
#define CTANL_SUM ( 218.42756568850280469996 + I * 2582.61959103398629689075)

#if defined(HAVE_CCOS)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ccos(stress_args_t *args)
{
	complex double sumccos = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumccos += shim_ccos(z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabs(sumccos - (complex double)CCOSD_SUM) > precision;
}
#endif

#if defined(HAVE_CCOSF)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ccosf(stress_args_t *args)
{
	complex double sumccos = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-3;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumccos += (complex double)shim_ccosf((complex float)z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabs(sumccos - (complex double)CCOSF_SUM) > precision;
}
#endif

#if defined(HAVE_CCOSL)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ccosl(stress_args_t *args)
{
	complex long double sumccos = 0.0L;
	complex long double z = -0.5 + 0.5 * I;
	complex long const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	long double precision;
	int i;

	switch (sizeof(precision)) {
	case 16:
	case 12:
		precision = 1E-8;
		break;
	default:
		precision = 1E-7;
		break;
	}

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumccos += shim_ccosl(z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabsl(sumccos - (complex long double)CCOSL_SUM) > precision;
}
#endif

#if defined(HAVE_CSIN)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_csin(stress_args_t *args)
{
	complex double sumcsin = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumcsin += shim_csin(z);
		z += dz;
	}
	stress_bogo_inc(args);

	return cabs(sumcsin - (complex double)CSIND_SUM) > precision;
}
#endif

#if defined(HAVE_CSINF)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_csinf(stress_args_t *args)
{
	complex double sumcsin = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-3;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumcsin += (complex double)shim_csinf((complex float)z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabs(sumcsin - (complex double)CSINF_SUM) > precision;
}
#endif

#if defined(HAVE_CSINL)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_csinl(stress_args_t *args)
{
	complex long double sumcsin = 0.0L;
	complex long double z = -0.5 + 0.5 * I;
	complex long const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	long double precision;
	int i;

	switch (sizeof(precision)) {
	case 16:
	case 12:
		precision = 1E-8;
		break;
	default:
		precision = 1E-7;
		break;
	}

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumcsin += shim_csinl(z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabsl(sumcsin - (complex long double)CSINL_SUM) > precision;
}
#endif

#if defined(HAVE_CTAN)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ctan(stress_args_t *args)
{
	complex double sumctan = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumctan += shim_ctan(z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabs(sumctan - (complex double)CTAND_SUM) > precision;
}
#endif

#if defined(HAVE_CTANF)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ctanf(stress_args_t *args)
{
	complex double sumctan = 0.0;
	complex double z = -0.5 + 0.5 * I;
	complex const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	const double precision = 1E-4;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumctan += (complex double)shim_ctanf((complex float)z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabs(sumctan - (complex double)CTANF_SUM) > precision;
}
#endif

#if defined(HAVE_CTANL)
static bool OPTIMIZE3 TARGET_CLONES stress_ctrig_ctanl(stress_args_t *args)
{
	complex long double sumctan = 0.0L;
	complex long double z = -0.5 + 0.5 * I;
	complex long const double dz = (1.0 / (double)STRESS_CTRIG_LOOPS - I / (2.0 * (double)STRESS_CTRIG_LOOPS));
	long double precision;
	int i;

	switch (sizeof(precision)) {
	case 16:
	case 12:
		precision = 1E-8;
		break;
	default:
		precision = 1E-7;
		break;
	}

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CTRIG_LOOPS; i++) {
		sumctan += shim_ctanl(z);
		z += dz;
	}
	stress_bogo_inc(args);
	return cabsl(sumctan - (complex long double)CTANL_SUM) > precision;
}
#endif

static bool stress_ctrig_all(stress_args_t *args);

static const stress_ctrig_method_t stress_ctrig_methods[] = {
	{ "all",	stress_ctrig_all },
#if defined(HAVE_CCOS)
	{ "ccos",	stress_ctrig_ccos },
#endif
#if defined(HAVE_CCOSF)
	{ "ccosf",	stress_ctrig_ccosf },
#endif
#if defined(HAVE_CCOSL)
	{ "ccosl",	stress_ctrig_ccosl },
#endif
#if defined(HAVE_CSIN)
	{ "csin",	stress_ctrig_csin },
#endif
#if defined(HAVE_CSINF)
	{ "csinf",	stress_ctrig_csinf },
#endif
#if defined(HAVE_CSINL)
	{ "csinl",	stress_ctrig_csinl },
#endif
#if defined(HAVE_CTAN)
	{ "ctan",	stress_ctrig_ctan },
#endif
#if defined(HAVE_CTANF)
	{ "ctanf",	stress_ctrig_ctanf },
#endif
#if defined(HAVE_CTANL)
	{ "ctanl",	stress_ctrig_ctanl },
#endif
};

stress_metrics_t stress_ctrig_metrics[SIZEOF_ARRAY(stress_ctrig_methods)];

static bool stress_ctrig_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_ctrig_methods[idx].trig_func(args);
	stress_ctrig_metrics[idx].duration += (stress_time_now() - t);
	stress_ctrig_metrics[idx].count += 1.0;
	if (ret) {
		if (idx != 0)
			pr_fail("trig: %s does not match expected checksum\n",
				stress_ctrig_methods[idx].name);
	}
	return ret;
}

static bool stress_ctrig_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_ctrig_methods); i++) {
		ret |= stress_ctrig_exercise(args, i);
	}
	return ret;
}

/*
 * stress_ctrig()
 *	stress system by various trig function calls
 */
static int stress_ctrig(stress_args_t *args)
{
	size_t i, j;
	size_t trig_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("trig-method", &trig_method);

	stress_zero_metrics(stress_ctrig_metrics, SIZEOF_ARRAY(stress_ctrig_metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_ctrig_exercise(args, trig_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_ctrig_metrics); i++) {
		if (stress_ctrig_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_CTRIG_LOOPS *
				stress_ctrig_metrics[i].count / stress_ctrig_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_ctrig_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const char *stress_ctrig_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_ctrig_methods)) ? stress_ctrig_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_ctrig_method, "ctrig-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_ctrig_method },
	END_OPT,
};

const stressor_info_t stress_ctrig_info = {
	.stressor = stress_ctrig,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const char *stress_ctrig_method(const size_t i)
{
	(void)i;

	return NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_ctrig_method, "ctrig-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_ctrig_method },
	END_OPT,
};

const stressor_info_t stress_ctrig_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without complex.h support"
};

#endif
