/*
 * Copyright (C) 2026      Colin Ian King.
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

#define STRESS_GAMMA_LOOPS	(10000)

typedef struct {
	const char *name;
	bool (*gamma_func)(stress_args_t *args);
} stress_gamma_method_t;

static const stress_help_t help[] = {
	{ NULL,	"gamma N",        "start N workers exercising gamma functions" },
	{ NULL, "gamma-method M", "select gamma function to exercise" },
	{ NULL,	"gamma-ops N",	  "stop after N gamma bogo operations" },
	{ NULL,	NULL,             NULL }
};

#if defined(HAVE_LGAMMA) ||	\
    defined(HAVE_LGAMMAF) ||	\
    defined(HAVE_LGAMMAL) ||	\
    defined(HAVE_TGAMMA) ||	\
    defined(HAVE_TGAMMAF) ||	\
    defined(HAVE_TGAMMAL)

static bool stress_gamma_all(stress_args_t *args);

#if defined(HAVE_LGAMMA)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_lgamma(stress_args_t *args)
{
	double sum = 0.0;
	double x = 0.01;
	const double dx = 1.0 / (double)STRESS_GAMMA_LOOPS;
	const double precision = 1E-1;
	const double res = 8631.1717319;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += shim_lgamma(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sum - res) > precision;
}
#endif

#if defined(HAVE_LGAMMAF)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_lgammaf(stress_args_t *args)
{
	double sum = 0.0;
	float x = 0.01F;
	const float dx = 1.0F / (float)STRESS_GAMMA_LOOPS;
	const double precision = 1E-4;
	const double res = 8631.1963;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += (double)shim_lgammaf(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sum - res) > precision;
}
#endif

#if defined(HAVE_LGAMMAL)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_lgammal(stress_args_t *args)
{
	long double sum = 0.0L;
	long double x = 0.01L;
	const long double dx = 1.0L / (long double)STRESS_GAMMA_LOOPS;
	const long double precision = 1E-7L;
	const long double res = 8631.171731871468190L;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += shim_lgammal(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabsl(sum - res) > precision;
}
#endif

#if defined(HAVE_TGAMMA)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_tgamma(stress_args_t *args)
{
	double sum = 0.0;
	double x = 0.01;
	const double dx = 1.0 / (double)STRESS_GAMMA_LOOPS;
	const double precision = 1E-6;
	const double res = 43791.009992;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += shim_tgamma(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sum - res) > precision;
}
#endif

#if defined(HAVE_TGAMMAF)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_tgammaf(stress_args_t *args)
{
	double sum = 0.0;
	float x = 0.01F;
	const float dx = 1.0F / (float)STRESS_GAMMA_LOOPS;
	const double precision = 1E-1;
	const double res = 43790.98598;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += (double)shim_tgammaf(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sum - res) > precision;
}
#endif

#if defined(HAVE_TGAMMAL)
static bool OPTIMIZE3 TARGET_CLONES stress_gamma_tgammal(stress_args_t *args)
{
	long double sum = 0.0L;
	long double x = 0.01L;
	const long double dx = 1.0L / (long double)STRESS_GAMMA_LOOPS;
	const long double precision = 1E-7L;
	const long double res = 43791.009991798602680L;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_GAMMA_LOOPS; i++) {
		sum += shim_tgammal(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabsl(sum - res) > precision;
}
#endif

static const stress_gamma_method_t stress_gamma_methods[] = {
	{ "all",	stress_gamma_all },
#if defined(HAVE_LGAMMA)
	{ "lgamma",	stress_gamma_lgamma },
#endif
#if defined(HAVE_LGAMMAF)
	{ "lgammaf",	stress_gamma_lgammaf },
#endif
#if defined(HAVE_LGAMMAL)
	{ "lgammal",	stress_gamma_lgammal },
#endif
#if defined(HAVE_TGAMMA)
	{ "tgamma",	stress_gamma_tgamma },
#endif
#if defined(HAVE_TGAMMAF)
	{ "tgammaf",	stress_gamma_tgammaf },
#endif
#if defined(HAVE_TGAMMAL)
	{ "tgammal",	stress_gamma_tgammal },
#endif
};

static stress_metrics_t stress_gamma_metrics[SIZEOF_ARRAY(stress_gamma_methods)];

static bool stress_gamma_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_gamma_methods[idx].gamma_func(args);
	stress_gamma_metrics[idx].duration += (stress_time_now() - t);
	stress_gamma_metrics[idx].count += 1.0;
	if (ret) {
		if (idx != 0)
			pr_fail("gamma: %s does not match expected checksum\n",
				stress_gamma_methods[idx].name);
	}
	return ret;
}

static bool stress_gamma_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_gamma_methods); i++) {
		ret |= stress_gamma_exercise(args, i);
	}
	return ret;
}

/*
 * stress_gamma()
 *	stress system by various gamma function calls
 */
static int stress_gamma(stress_args_t *args)
{
	size_t i;
	size_t gamma_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_setting_get("gamma-method", &gamma_method);

	stress_zero_metrics(stress_gamma_metrics, SIZEOF_ARRAY(stress_gamma_metrics));

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		if (stress_gamma_exercise(args, gamma_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 1; i < SIZEOF_ARRAY(stress_gamma_metrics); i++) {
		if (stress_gamma_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_GAMMA_LOOPS *
				stress_gamma_metrics[i].count / stress_gamma_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_gamma_methods[i].name);
			stress_metrics_set(args, buf, rate, STRESS_METRIC_HARMONIC_MEAN);
		}
	}
	return rc;
}

static const char *stress_gamma_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_gamma_methods)) ? stress_gamma_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_gamma_method, "gamma-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_gamma_method },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("bogo-ops-stable"),
	STRESS_EX_FEATURE("hot-package"),
	STRESS_EX_FEATURE("fp"),
	STRESS_EX_FEATURE("user-time"),

	STRESS_EX_LIBRARY("m"),

	STRESS_EX_END,
};

const stressor_info_t stress_gamma_info = {
	.stressor = stress_gamma,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.max_metrics_items = SIZEOF_ARRAY(stress_gamma_methods),
	.exercises = exercises,
};

#else

static const stress_opt_t opts[] = {
	{ OPT_gamma_method, "gamma-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method},
	END_OPT,
};

const stressor_info_t stress_gamma_info = {
        .stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
        .unimplemented_reason = "built without lgamma or tgamma functions",
};

#endif
