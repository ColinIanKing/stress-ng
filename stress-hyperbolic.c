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

#define STRESS_HYPERBOLIC_LOOPS	(10000)

typedef struct {
	const char *name;
	bool (*hyperbolic_func)(stress_args_t *args);
} stress_hyperbolic_method_t;

static const stress_help_t help[] = {
	{ NULL,	"hyperbolic N",	 	"start N workers exercisinhg hyberbolic functions" },
	{ NULL,	"hyperbolic-ops N",	"stop after N hyperbolic bogo hyperbolic function operations" },
	{ NULL, "hyperbolic-method M",	"select hyperbolic function to exercise" },
	{ NULL,	NULL,		 	NULL }
};

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_cosh(stress_args_t *args)
{
	double sumcosh = 0.0;
	double x = -1.0;
	const double dx = 2.0 / (double)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumcosh += shim_cosh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumcosh - (double)11752.01197561116714496166) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_coshf(stress_args_t *args)
{
	double sumcosh = 0.0;
	double x = -1.0;
	const double dx = 2.0 / (float)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-4;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumcosh += (double)shim_coshf((float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumcosh - (double)11752.01196670532226562500) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_coshl(stress_args_t *args)
{
	long double sumcosh = 0.0L;
	long double x = -1.0L;
	const long double dx = 2.0L / (long double)STRESS_HYPERBOLIC_LOOPS;
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
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumcosh += shim_coshl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabsl(sumcosh - (long double)11752.01197561138762193167) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_sinh(stress_args_t *args)
{
	double sumsinh = 0.0;
	double x = -1.0;
	const double dx = 2.0 / (double)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumsinh += shim_sinh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumsinh - (double)-1.17520119455734528557) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_sinhf(stress_args_t *args)
{
	double sumsinh = 0.0;
	double x = -1.0;
	const double dx = 2.0 / (float)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-4;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumsinh += (double)shim_sinhf((float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumsinh - (double)-1.17520117759704589844) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_sinhl(stress_args_t *args)
{
	long double sumsinh = 0.0L;
	long double x = -1.0L;
	const long double dx = 2.0L / (long double)STRESS_HYPERBOLIC_LOOPS;
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
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumsinh += shim_sinhl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabsl(sumsinh - (long double)-1.1752011936441843611) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_tanh(stress_args_t *args)
{
	double sumtanh = 0.0;
	double x = -10.0;
	const double dx = 20.0 / (double)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumtanh += shim_tanh(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumtanh - (double)-1.0) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_tanhf(stress_args_t *args)
{
	double sumtanh = 0.0;
	double x = -10.0;
	const double dx = 20.0 / (double)STRESS_HYPERBOLIC_LOOPS;
	const double precision = 1E-5;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumtanh += shim_tanhf((float)x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabs(sumtanh - (double)-1.0) > precision;
}

static bool OPTIMIZE3 TARGET_CLONES stress_hyperbolic_tanhl(stress_args_t *args)
{
	long double sumtanh = 0.0;
	long double x = -10.0;
	const long double dx = 20.0L / (long double)STRESS_HYPERBOLIC_LOOPS;
	const long double precision = 1E-7;
	int i;

PRAGMA_UNROLL_N(8)
	for (i = 0; i < STRESS_HYPERBOLIC_LOOPS; i++) {
		sumtanh += shim_tanhl(x);
		x += dx;
	}
	stress_bogo_inc(args);
	return shim_fabsl(sumtanh - (long double)-1.0) > precision;
}

static bool stress_hyperbolic_all(stress_args_t *args);

static const stress_hyperbolic_method_t stress_hyperbolic_methods[] = {
	{ "all",	stress_hyperbolic_all },
	{ "cosh",	stress_hyperbolic_cosh	},
	{ "coshf",	stress_hyperbolic_coshf },
	{ "coshl",	stress_hyperbolic_coshl },
	{ "sinh",	stress_hyperbolic_sinh	},
	{ "sinhf",	stress_hyperbolic_sinhf },
	{ "sinhl",	stress_hyperbolic_sinhl },
	{ "tanh",	stress_hyperbolic_tanh },
	{ "tanhf",	stress_hyperbolic_tanhf },
	{ "tanhl",	stress_hyperbolic_tanhl },
};

stress_metrics_t stress_hyperbolic_metrics[SIZEOF_ARRAY(stress_hyperbolic_methods)];

static bool stress_hyperbolic_exercise(stress_args_t *args, const size_t idx)
{
	bool ret;
	const double t = stress_time_now();

	ret = stress_hyperbolic_methods[idx].hyperbolic_func(args);
	stress_hyperbolic_metrics[idx].duration += (stress_time_now() - t);
	stress_hyperbolic_metrics[idx].count += 1.0;
	if (ret) {
		if (idx != 0)
			pr_fail("hyperbolic: %s does not match expected checksum\n",
				stress_hyperbolic_methods[idx].name);
	}
	return ret;
}

static bool stress_hyperbolic_all(stress_args_t *args)
{
	size_t i;
	bool ret = false;

	for (i = 1; i < SIZEOF_ARRAY(stress_hyperbolic_methods); i++) {
		ret |= stress_hyperbolic_exercise(args, i);
	}
	return ret;
}

/*
 * stress_hyperbolic()
 *	stress system by various hyperbolic function calls
 */
static int stress_hyperbolic(stress_args_t *args)
{
	size_t i, j;
	size_t hyperbolic_method = 0;
	int rc = EXIT_SUCCESS;

	(void)stress_get_setting("hyperbolic-method", &hyperbolic_method);

	stress_zero_metrics(stress_hyperbolic_metrics, SIZEOF_ARRAY(stress_hyperbolic_metrics));

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		if (stress_hyperbolic_exercise(args, hyperbolic_method)) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_hyperbolic_metrics); i++) {
		if (stress_hyperbolic_metrics[i].duration > 0.0) {
			char buf[80];
			const double rate = (double)STRESS_HYPERBOLIC_LOOPS *
				stress_hyperbolic_metrics[i].count / stress_hyperbolic_metrics[i].duration;

			(void)snprintf(buf, sizeof(buf), "%s ops per second", stress_hyperbolic_methods[i].name);
			stress_metrics_set(args, j, buf,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const char *stress_hyperbolic_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(stress_hyperbolic_methods)) ? stress_hyperbolic_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_hyperbolic_method, "hyperbolic-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_hyperbolic_method },
	END_OPT,
};

const stressor_info_t stress_hyperbolic_info = {
	.stressor = stress_hyperbolic,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
