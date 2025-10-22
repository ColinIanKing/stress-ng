/*
 * Copyright (C) 2023-2025 Colin Ian King.
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
#include "stress-eigen-ops.h"
#include <math.h>

#define MIN_MATRIX_SIZE		(2)
#define MAX_MATRIX_SIZE		(1024)
#define DEFAULT_MATRIX_SIZE	(32)

static const stress_help_t help[] = {
	{ NULL,	"eigen N",	  "start N workers exercising eigen operations" },
	{ NULL,	"eigen-method M", "specify eigen stress method M, default is all" },
	{ NULL,	"eigen-ops N",	  "stop after N matrix bogo operations" },
	{ NULL,	"eigen-size N",	  "specify the size of the N x N eigen" },
	{ NULL,	NULL,		  NULL }
};

#if defined(HAVE_EIGEN)

/*
 *  the eigen stress test has different classes of matrix stressor
 */
typedef int (*stress_eigen_func_t)(const size_t size, double *duration, double *count);

typedef struct {
	const char			*name;		/* human readable form of stressor */
	const stress_eigen_func_t	func;		/* method functions */
} stress_eigen_method_info_t;

static const char *current_method = NULL;		/* current eigen method */
static size_t method_all_index;				/* all method index */
static const stress_eigen_method_info_t eigen_methods[];

/*
 *  stress_eigen_all()
 *	iterate over all cpu stressors
 */
static int stress_eigen_all();

/*
 * Table of eigen stress methods, ordered x by y and y by x
 */
static const stress_eigen_method_info_t eigen_methods[] = {
	{ "all",			stress_eigen_all },
	{ "add-longdouble",		eigen_add_long_double, },
	{ "add-double",			eigen_add_double, },
	{ "add-float",			eigen_add_float, },
	{ "determinant-longdouble",	eigen_determinant_long_double, },
	{ "determinant-double",		eigen_determinant_double, },
	{ "determinant-float",		eigen_determinant_float, },
	{ "inverse-longdouble",		eigen_inverse_long_double, },
	{ "inverse-double",		eigen_inverse_double, },
	{ "inverse-float",		eigen_inverse_float, },
	{ "multiply-longdouble",	eigen_multiply_long_double, },
	{ "multiply-double",		eigen_multiply_double, },
	{ "multiply-float",		eigen_multiply_float, },
	{ "transpose-longdouble",	eigen_transpose_long_double, },
	{ "transpose-double",		eigen_transpose_double, },
	{ "transpose-float",		eigen_transpose_float, },
};

#define NUM_EIGEN_METHODS	(SIZEOF_ARRAY(eigen_methods))

static stress_metrics_t eigen_metrics[NUM_EIGEN_METHODS];

static const char *stress_eigen_method(const size_t i)
{
	return (i < NUM_EIGEN_METHODS) ? eigen_methods[i].name : NULL;
}

/*
 *  stress_eigen_all()
 *	iterate over all matrix stressors
 */
static int stress_eigen_all(
	const size_t size,
	double *duration,
	double *count)
{
	int rc;

	(void)duration;
	(void)count;

	current_method = eigen_methods[method_all_index].name;

	rc = eigen_methods[method_all_index].func(size,
		&eigen_metrics[method_all_index].duration,
		&eigen_metrics[method_all_index].count);

	return rc;
}

static inline int stress_eigen_exercise(
	stress_args_t *args,
	const size_t eigen_method,
	const size_t eigen_size)
{
	int rc = EXIT_SUCCESS;
	const stress_eigen_func_t func = eigen_methods[eigen_method].func;
	const char *name = eigen_methods[eigen_method].name;
	double mantissa;
	uint64_t exponent;

	register size_t i, j;
	method_all_index = 1;

	stress_zero_metrics(eigen_metrics, NUM_EIGEN_METHODS);

	current_method = eigen_methods[eigen_method].name;

	do {
		int ret;
		ret = func(eigen_size,
			&eigen_metrics[eigen_method].duration,
			&eigen_metrics[eigen_method].count);

		if (ret < 0) {
			pr_inf("%s: eigen matrix library failure with %s, skipping stressor\n", args->name, name);
			rc = EXIT_NO_RESOURCE;
			break;
		} else {
			if (ret == EXIT_FAILURE) {
				pr_fail("%s: eigen matrix operation %s check failed\n", args->name, name);
				rc = EXIT_FAILURE;
				break;
			}
		}
		stress_bogo_inc(args);
                if (eigen_method == 0) {
                        method_all_index++;
                        if (method_all_index >= NUM_EIGEN_METHODS)
                                method_all_index = 1;
		}
	} while (stress_continue(args));

	mantissa = 1.0;
	exponent = 0;

	/* Dump metrics except for 'all' method */
	for (i = 1, j = 0; i < NUM_EIGEN_METHODS; i++) {
		if (eigen_metrics[i].duration > 0.0) {
			char msg[64];
			int e;
			const double rate = eigen_metrics[i].count / eigen_metrics[i].duration;
			const double f = frexp(rate, &e);

			mantissa *= f;
			exponent += e;

			(void)snprintf(msg, sizeof(msg), "%s matrix %zu x %zu ops per sec",
				eigen_methods[i].name, eigen_size, eigen_size);
			stress_metrics_set(args, j, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	if (j > 0) {
		double inverse_n = 1.0 / (double)j;
		double geomean = pow(mantissa, inverse_n) *
				 pow(2.0, (double)exponent * inverse_n);

		pr_dbg("%s: %.2f eigen ops per second (geometric mean of per stressor bogo-op rates)\n",
			args->name, geomean);
	}

	return rc;
}

/*
 *  stress_eigen()
 *	stress CPU by doing floating point math ops
 */
static int stress_eigen(stress_args_t *args)
{
	size_t eigen_method = 0;	/* All method */
	size_t eigen_size = DEFAULT_MATRIX_SIZE;
	int rc;

	(void)stress_get_setting("eigen-method", &eigen_method);
	(void)stress_get_setting("eigen-size", &eigen_size);

	if (!stress_get_setting("eigen-size", &eigen_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			eigen_size = MAX_MATRIX_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			eigen_size = MIN_MATRIX_SIZE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_eigen_exercise(args, eigen_method, eigen_size);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_eigen_method, "eigen-method",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_eigen_method },
	{ OPT_eigen_size,   "eigen-size",    TYPE_ID_SIZE_T, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_eigen_info = {
	.stressor = stress_eigen,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

static const stress_opt_t opts[] = {
	{ OPT_eigen_method, "eigen-method",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	{ OPT_eigen_size,   "eigen-size",    TYPE_ID_SIZE_T, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE, NULL },
	END_OPT,
};

const stressor_info_t stress_eigen_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "eigen C++ library, headers or g++ compiler not used"
};
#endif
