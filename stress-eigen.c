/*
 * Copyright (C) 2023-2024 Colin Ian King.
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

#define MIN_MATRIX_SIZE		(2)
#define MAX_MATRIX_SIZE		(1024)
#define DEFAULT_MATRIX_SIZE	(32)

static const stress_help_t help[] = {
	{ NULL,	"eigen N",		"start N workers exercising eigen operations" },
	{ NULL,	"eigen-method M",	"specify eigen stress method M, default is all" },
	{ NULL,	"eigen-ops N",		"stop after N maxtrix bogo operations" },
	{ NULL,	"eigen-size N",	"specify the size of the N x N eigen" },
	{ NULL,	NULL,			NULL }
};

static int stress_set_eigen_size(const char *opt)
{
	size_t eigen_size;

	eigen_size = stress_get_uint64(opt);
	stress_check_range("eigen-size", eigen_size,
		MIN_MATRIX_SIZE, MAX_MATRIX_SIZE);
	return stress_set_setting("eigen-size", TYPE_ID_SIZE_T, &eigen_size);
}

#if defined(HAVE_EIGEN)

/*
 *  the eigen stress test has different classes of maxtrix stressor
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

static stress_metrics_t eigen_metrics[SIZEOF_ARRAY(eigen_methods)];

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

/*
 *  stress_set_eigen_method()
 *	get the default eigen stress method
 */
static int stress_set_eigen_method(const char *name)
{
	size_t eigen_method;

	for (eigen_method = 0; eigen_method < SIZEOF_ARRAY(eigen_methods); eigen_method++) {
		if (!strcmp(eigen_methods[eigen_method].name, name)) {
			stress_set_setting("eigen-method", TYPE_ID_SIZE_T, &eigen_method);
			return 0;
		}
	}

	(void)fprintf(stderr, "eigen-method must be one of:");
	for (eigen_method = 0; eigen_method < SIZEOF_ARRAY(eigen_methods); eigen_method++)
		(void)fprintf(stderr, " %s", eigen_methods[eigen_method].name);
	(void)fprintf(stderr, "\n");

	return -1;
}

static inline int stress_eigen_exercise(
	stress_args_t *args,
	const size_t eigen_method,
	const size_t eigen_size)
{
	int rc = EXIT_SUCCESS;
	const size_t num_eigen_methods = SIZEOF_ARRAY(eigen_methods);
	const stress_eigen_func_t func = eigen_methods[eigen_method].func;
	const char *name = eigen_methods[eigen_method].name;

	register size_t i, j;
	method_all_index = 1;

	for (i = 0; i < SIZEOF_ARRAY(eigen_metrics); i++) {
		eigen_metrics[i].duration = 0.0;
		eigen_metrics[i].count = 0.0;
	}

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
                        if (method_all_index >= SIZEOF_ARRAY(eigen_methods))
                                method_all_index = 1;
		}
	} while (stress_continue(args));

	/* Dump metrics except for 'all' method */
	for (i = 1, j = 0; i < num_eigen_methods; i++) {
		if (eigen_metrics[i].duration > 0.0) {
			char msg[64];
			const double rate = eigen_metrics[i].count / eigen_metrics[i].duration;

			(void)snprintf(msg, sizeof(msg), "%s matrix %zd x %zd ops per sec",
				eigen_methods[i].name, eigen_size, eigen_size);
			stress_metrics_set(args, j, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
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

	stress_set_proc_state(args->name, STRESS_STATE_RUN);
	stress_sync_start_wait(args);

	rc = stress_eigen_exercise(args, eigen_method, eigen_size);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_eigen_method,	stress_set_eigen_method },
	{ OPT_eigen_size,	stress_set_eigen_size },
	{ 0,			NULL },
};

stressor_info_t stress_eigen_info = {
	.stressor = stress_eigen,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};

#else

/*
 *  stress_set_eigen_method()
 *	get the default eigen stress method
 */
static int stress_set_eigen_method(const char *name)
{
	(void)name;

	(void)fprintf(stderr, "eigen stressor not implemented, eigen-method '%s' not available\n", name);
	return -1;
}


static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_eigen_method,	stress_set_eigen_method },
	{ OPT_eigen_size,	stress_set_eigen_size },
	{ 0,			NULL },
};

stressor_info_t stress_eigen_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_COMPUTE,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.unimplemented_reason = "eigen C++ library, headers or g++ compiler not used"
};
#endif
