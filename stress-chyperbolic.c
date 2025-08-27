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

#define STRESS_CHYPERBOLIC_LOOPS	(10000)

typedef struct {
	const char *name;
	bool (*chyperbolic_func)(stress_args_t *args);
} stress_chyperbolic_method_t;

static const stress_help_t help[] = {
	{ NULL,	"chyperbolic N",	"start N workers exercising complex hyberbolic functions" },
	{ NULL,	"chyperbolic-ops N",	"stop after N chyperbolic bogo complex hyperbolic function operations" },
	{ NULL, "chyperbolic-method M",	"select complex hyperbolic function to exercise" },
	{ NULL,	NULL,		 	NULL }
};

#if defined(HAVE_COMPLEX_H)

#define CCOSHD_SUM (11319.64446039962604118045 + I * 865.40045684982658258377)
#define CCOSHF_SUM (11319.64446061849594116211 + I * 865.40045857543373131193)
#define CCOSHL_SUM (11319.64446039942314836679 + I * 865.40045684969749112403)

#define CSINHD_SUM (-5324.57218954220297746360 - I * 2661.75257615712280312437)
#define CSINHF_SUM (-5324.57219118710781913251 - I * 2661.75257824994332622737)
#define CSINHL_SUM (-5324.57218954160808443987 - I * 2661.75257615727348525780)

#define CTANHD_SUM (-4515.30135717186658439459 - I * 2257.18713612209239727235)
#define CTANHF_SUM (-4515.30135638175124768168 - I * 2257.18713951996687683277)
#define CTANHL_SUM (-4515.30135717137690498646 - I * 2257.18713612237787513592)

#if defined(HAVE_CCOSH)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ccosh(stress_args_t *args)
{
	complex double sumccosh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumccosh += shim_ccosh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumccosh - (complex double)CCOSHD_SUM) > precision;
}
#endif

#if defined(HAVE_CCOSHF)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ccoshf(stress_args_t *args)
{
	complex double sumccosh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-4;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumccosh += (complex double)shim_ccoshf((complex float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumccosh - (complex double)CCOSHF_SUM) > precision;
}
#endif

#if defined(HAVE_CCOSHL)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ccoshl(stress_args_t *args)
{
	complex long double sumccosh = 0.0L;
	complex long double x = -1.0L;
	complex const long double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
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
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumccosh += shim_ccoshl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabsl(sumccosh - (complex double)CCOSHL_SUM) > precision;
}
#endif

#if defined(HAVE_CSINH)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_csinh(stress_args_t *args)
{
	complex double sumcsinh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumcsinh += shim_csinh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumcsinh - (complex double)CSINHD_SUM) > precision;
}
#endif

#if defined(HAVE_CSINHF)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_csinhf(stress_args_t *args)
{
	complex double sumcsinh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-4;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumcsinh += (complex double)shim_csinhf((complex float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumcsinh - (complex double)CSINHF_SUM) > precision;
}
#endif

#if defined(HAVE_CSINHL)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_csinhl(stress_args_t *args)
{
	complex long double sumcsinh = 0.0L;
	complex long double x = -1.0L;
	complex long const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
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
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumcsinh += shim_csinhl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabsl(sumcsinh - (complex double)CSINHL_SUM) > precision;
}
#endif

#if defined(HAVE_CTANH)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ctanh(stress_args_t *args)
{
	complex double sumctanh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumctanh += shim_ctanh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumctanh - (complex double)CTANHD_SUM) > precision;
}
#endif

#if defined(HAVE_CTANHF)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ctanhf(stress_args_t *args)
{
	complex double sumctanh = 0.0;
	complex double x = -1.0;
	complex const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const double precision = 1E-5;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumctanh += shim_ctanhf((complex float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabs(sumctanh - (complex double)CTANHD_SUM) > precision;
}
#endif

#if defined(HAVE_CTANHL)
static bool OPTIMIZE3 TARGET_CLONES stress_chyperbolic_ctanhl(stress_args_t *args)
{
	complex long double sumctanh = 0.0;
	complex long double x = -1.0;
	complex long const double dx = (1.0 / (double)STRESS_CHYPERBOLIC_LOOPS - I / (2.0 * (double)STRESS_CHYPERBOLIC_LOOPS));
	const long double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_CHYPERBOLIC_LOOPS; i++) {
		sumctanh += shim_ctanhl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return cabsl(sumctanh - (complex double)CTANHL_SUM) > precision;
}
#endif

static bool stress_chyperbolic_all(stress_args_t *args);

static const stress_chyperbolic_method_t stress_chyperbolic_methods[] = {
	{ "all",	stress_chyperbolic_all },
#if defined(HAVE_CCOSH)
	{ "ccosh",	stress_chyperbolic_ccosh },
#endif
#if defined(HAVE_CCOSHF)
	{ "ccoshf",	stress_chyperbolic_ccoshf },
#endif
#if defined(HAVE_CCOSHL)
	{ "ccoshl",	stress_chyperbolic_ccoshl },
#endif
#if defined(HAVE_CSINH)
	{ "csinh",	stress_chyperbolic_csinh },
#endif
#if defined(HAVE_CSINHF)
	{ "csinhf",	stress_chyperbolic_csinhf },
#endif
#if defined(HAVE_CSINHL)
	{ "csinhl",	stress_chyperbolic_csinhl },
#endif
#if defined(HAVE_CTANH)
	{ "ctanh",	stress_chyperbolic_ctanh },
#endif
#if defined(HAVE_CTANHF)
	{ "ctanhf",	stress_chyperbolic_ctanhf },
#endif
#if defined(HAVE_CTANHL)
	{ "ctanhl",	stress_chyperbolic_ctanhl },
#endif
};

stress_metrics_t stress_chyperbolic_metrics[SIZEOF_ARRAY(stress_chyperbolic_methods)];

static bool stress_chyperbolic_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_chyperbolic_methods[idx].chyperbolic_func(args);
	stress_chyperbolic_metrics[idx].duration += (stress_time_now() - t);
	stress_chyperbolic_metrics[idx].count += 1.0;
	if (ret) {
		if (idx != 0)
			pr_fail("chyperbolic: %s does not match expected checksum\n",
				stress_chyperbolic_methods[idx].name);
	}
	return ret;
}

static bool stress_chyperbolic_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_chyperbolic_methods); i++) {
		ret |= stress_chyperbolic_exercise(args, i);
	}
	return ret;
}

/*
 * stress_chyperbolic()
 *	stress system by various chyperbolic function calls
 */
static int stress_chyperbolic(stress_args_t *args)
{
	size_t i, j;
	size_t chyperbolic_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("chyperbolic-method", &chyperbolic_method);

	stress_zero_metrics(stress_chyperbolic_metrics, SIZEOF_ARRAY(stress_chyperbolic_metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_chyperbolic_exercise(args, chyperbolic_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_chyperbolic_metrics); i++) {
		if (stress_chyperbolic_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_CHYPERBOLIC_LOOPS *
				stress_chyperbolic_metrics[i].count / stress_chyperbolic_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_chyperbolic_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const char *stress_chyperbolic_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_chyperbolic_methods)) ? stress_chyperbolic_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_chyperbolic_method, "chyperbolic-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_chyperbolic_method },
	END_OPT,
};

const stressor_info_t stress_chyperbolic_info = {
	.stressor = stress_chyperbolic,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const char *stress_chyperbolic_method(const size_t i)
{
	return NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_chyperbolic_method, "chyperbolic-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_chyperbolic_method },
	END_OPT,
};

const stressor_info_t stress_chyperbolic_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "built without complex.h support"
};

#endif
