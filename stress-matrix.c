/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King.
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
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-pragma.h"
#include "core-put.h"
#include "core-target-clones.h"

#include <math.h>

#define MIN_MATRIX_SIZE		(16)
#define MAX_MATRIX_SIZE		(8192)
#define DEFAULT_MATRIX_SIZE	(128)

static const stress_help_t help[] = {
	{ NULL,	"matrix N",		"start N workers exercising matrix operations" },
	{ NULL,	"matrix-method M",	"specify matrix stress method M, default is all" },
	{ NULL,	"matrix-ops N",		"stop after N matrix bogo operations" },
	{ NULL,	"matrix-size N",	"specify the size of the N x N matrix" },
	{ NULL,	"matrix-yx",		"matrix operation is y by x instead of x by y" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_VLA_ARG)

typedef float	stress_matrix_type_t;

/*
 *  the matrix stress test has different classes of matrix stressor
 */
typedef void (*stress_matrix_func_t)(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n]);

typedef struct {
	const char			*name;		/* human readable form of stressor */
	const stress_matrix_func_t	func[2];	/* method functions, x by y, y by x */
} stress_matrix_method_info_t;

static const char *current_method = NULL;		/* current matrix method */
static size_t method_all_index;				/* all method index */

static const stress_matrix_method_info_t matrix_methods[];

/*
 *  stress_matrix_xy_prod()
 *	matrix product
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_prod(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

PRAGMA_UNROLL_N(8)
			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * b[k][j];
			}
		}
	}
}

/*
 *  stress_matrix_yx_prod()
 *	matrix product
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_prod(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * b[k][j];
			}
		}
	}
}

/*
 *  stress_matrix_xy_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_add(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] + b[i][j];
		}
	}
}

/*
 *  stress_matrix_yx_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_add(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] + b[i][j];
		}
	}
}

/*
 *  stress_matrix_xy_sub()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_sub(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] - b[i][j];
		}
	}
}

/*
 *  stress_matrix_xy_sub()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_sub(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {

		register size_t i;
		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] - b[i][j];
		}
	}
}

/*
 *  stress_matrix_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_trans(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],	/* Ignored */
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = a[j][i];
		}
	}
}

/*
 *  stress_matrix_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_trans(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],	/* Ignored */
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[j][i];
		}
	}
}

/*
 *  stress_matrix_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_mult(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;
	stress_matrix_type_t v = b[0][0];

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = v * a[i][j];
		}
	}
}

/*
 *  stress_matrix_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_mult(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;
	stress_matrix_type_t v = b[0][0];

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = v * a[i][j];
		}
	}
}

/*
 *  stress_matrix_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_div(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;
	stress_matrix_type_t v = b[0][0];

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] / v;
		}
	}
}

/*
 *  stress_matrix_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_div(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;
	stress_matrix_type_t v = b[0][0];

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] / v;
		}
	}
}

/*
 *  stress_matrix_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_hadamard(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] * b[i][j];
		}
	}
}

/*
 *  stress_matrix_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_hadamard(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] * b[i][j];
		}
	}
}

/*
 *  stress_matrix_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_frobenius(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;
	stress_matrix_type_t sum = 0.0;

	(void)r;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			sum += a[i][j] * b[i][j];
		}
	}
	stress_float_put((float)sum);
}

/*
 *  stress_matrix_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_frobenius(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;
	stress_matrix_type_t sum = 0.0;

	(void)r;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			sum += a[i][j] * b[i][j];
		}
	}
	stress_float_put((float)sum);
}

/*
 *  stress_matrix_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_copy(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		/* unrolling does not help, the following turns into a memcpy */
		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j];
		}
	}
}

/*
 *  stress_matrix_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_copy(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		/* unrolling does not help, the following turns into a memcpy */
		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j];
		}
	}
}

/*
 *  stress_matrix_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_mean(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = (a[i][j] + b[i][j]) / (stress_matrix_type_t)2.0;
		}
	}
}

/*
 *  stress_matrix_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_mean(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = (a[i][j] + b[i][j]) / (stress_matrix_type_t)2.0;
		}
	}
}

/*
 *  stress_matrix_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_zero(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		/* unrolling does not help, the following turns into a memset */
		for (j = 0; j < n; j++) {
			r[i][j] = 0.0;
		}
	}
}

/*
 *  stress_matrix_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_zero(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		/* unrolling does not help, the following turns into a memset */
		for (i = 0; i < n; i++) {
			r[i][j] = 0.0;
		}
	}
}

/*
 *  stress_matrix_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_negate(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = -a[i][j];
		}
	}
}

/*
 *  stress_matrix_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_negate(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = -a[i][j];
		}
	}
}

/*
 *  stress_matrix_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_identity(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

PRAGMA_UNROLL_N(8)
		for (j = 0; j < n; j++) {
			r[i][j] = (i == j) ? 1.0 : 0.0;
		}
	}
}

/*
 *  stress_matrix_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_identity(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = (i == j) ? 1.0 : 0.0;
		}
	}
}

/*
 *  stress_matrix_xy_square()
 *	matrix square, r = a x a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_square(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

PRAGMA_UNROLL_N(8)
			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * a[k][j];
			}
		}
	}
}

/*
 *  stress_matrix_yx_square()
 *	matrix square, r = a x a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_square(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	size_t j;

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * a[k][j];
			}
		}
	}
}

/*
 *  stress_matrix_xy_all()
 *	iterate over all matrix stressors
 */
static void stress_matrix_xy_all(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n]);

/*
 *  stress_matrix_yx_all()
 *	iterate over all matrix stressors
 */
static void OPTIMIZE3 stress_matrix_yx_all(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n]);

/*
 * Table of matrix stress methods, ordered x by y and y by x
 */
static const stress_matrix_method_info_t matrix_methods[] = {
	{ "all",		{ stress_matrix_xy_all,		stress_matrix_yx_all } },/* Special "all" test */

	{ "add",		{ stress_matrix_xy_add,		stress_matrix_yx_add } },
	{ "copy",		{ stress_matrix_xy_copy,	stress_matrix_yx_copy } },
	{ "div",		{ stress_matrix_xy_div,		stress_matrix_yx_div } },
	{ "frobenius",		{ stress_matrix_xy_frobenius,	stress_matrix_yx_frobenius } },
	{ "hadamard",		{ stress_matrix_xy_hadamard,	stress_matrix_yx_hadamard } },
	{ "identity",		{ stress_matrix_xy_identity,	stress_matrix_yx_identity } },
	{ "mean",		{ stress_matrix_xy_mean,	stress_matrix_yx_mean } },
	{ "mult",		{ stress_matrix_xy_mult,	stress_matrix_yx_mult } },
	{ "negate",		{ stress_matrix_xy_negate,	stress_matrix_yx_negate } },
	{ "prod",		{ stress_matrix_xy_prod,	stress_matrix_yx_prod } },
	{ "sub",		{ stress_matrix_xy_sub,		stress_matrix_yx_sub } },
	{ "square",		{ stress_matrix_xy_square,	stress_matrix_yx_square } },
	{ "trans",		{ stress_matrix_xy_trans,	stress_matrix_yx_trans } },
	{ "zero",		{ stress_matrix_xy_zero,	stress_matrix_yx_zero } },
};

static stress_metrics_t matrix_metrics[SIZEOF_ARRAY(matrix_methods)];

/*
 *  stress_matrix_xy_all()
 *	iterate over all matrix stressors
 */
static void OPTIMIZE3 stress_matrix_xy_all(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	double t;

	current_method = matrix_methods[method_all_index].name;

	t = stress_time_now();
	matrix_methods[method_all_index].func[0](n, a, b, r);
	matrix_metrics[method_all_index].duration += stress_time_now() - t;
	matrix_metrics[method_all_index].count += 1.0;
}

/*
 *  stress_matrix_yx_all()
 *	iterate over all matrix stressors
 */
static void OPTIMIZE3 stress_matrix_yx_all(
	const size_t n,
	stress_matrix_type_t a[RESTRICT n][n],
	stress_matrix_type_t b[RESTRICT n][n],
	stress_matrix_type_t r[RESTRICT n][n])
{
	double t;

	current_method = matrix_methods[method_all_index].name;

	t = stress_time_now();
	matrix_methods[method_all_index].func[1](n, a, b, r);
	matrix_metrics[method_all_index].duration += stress_time_now() - t;
	matrix_metrics[method_all_index].count += 1.0;
}

static inline size_t round_up(size_t page_size, size_t n)
{
	page_size = (page_size == 0) ? 4096 : page_size;

	return (n + page_size - 1) & (~(page_size -1));
}

/*
 *  stress_matrix_data()
 *`	generate some random data scaled by v
 */
static inline stress_matrix_type_t stress_matrix_data(const stress_matrix_type_t v)
{
	const uint64_t r = stress_mwc64();

	return v * (stress_matrix_type_t)r;
}

static inline int stress_matrix_exercise(
	stress_args_t *args,
	const size_t matrix_method,
	const size_t matrix_yx,
	const size_t n)
{
	typedef stress_matrix_type_t (*matrix_ptr_t)[n];

	int ret = EXIT_NO_RESOURCE;
	const size_t matrix_size = sizeof(stress_matrix_type_t) * n * n;
	const size_t matrix_mmap_size = round_up(args->page_size, matrix_size);
	const size_t num_matrix_methods = SIZEOF_ARRAY(matrix_methods);
	const stress_matrix_func_t func = matrix_methods[matrix_method].func[matrix_yx];
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	double mantissa;
	int64_t exponent;

	matrix_ptr_t a, b = NULL, r = NULL, s = NULL;
	register size_t i, j;
	const stress_matrix_type_t v = 65535 / (stress_matrix_type_t)((uint64_t)~0);
	method_all_index = 1;

	current_method = matrix_methods[matrix_method].name;

	stress_zero_metrics(matrix_metrics, num_matrix_methods);

	a = (matrix_ptr_t)stress_mmap_populate(NULL, matrix_mmap_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (a == MAP_FAILED) {
		pr_fail("%s: matrix allocation failed, out of memory%s, errno=%d (%s)\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		goto tidy_ret;
	}
	(void)stress_madvise_collapse(a, matrix_mmap_size);
	stress_set_vma_anon_name(a, matrix_mmap_size, "matrix-a");

	b = (matrix_ptr_t)stress_mmap_populate(NULL, matrix_mmap_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (b == MAP_FAILED) {
		pr_fail("%s: matrix allocation failed, out of memory%s, errno=%d (%s)\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		goto tidy_a;
	}
	(void)stress_madvise_collapse(b, matrix_mmap_size);
	stress_set_vma_anon_name(b, matrix_mmap_size, "matrix-b");

	r = (matrix_ptr_t)stress_mmap_populate(NULL, matrix_mmap_size,
		PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (r == MAP_FAILED) {
		pr_fail("%s: matrix allocation failed, out of memory%s, errno=%d (%s)\n",
			args->name, stress_get_memfree_str(),
			errno, strerror(errno));
		goto tidy_b;
	}
	(void)stress_madvise_collapse(r, matrix_mmap_size);
	stress_set_vma_anon_name(r, matrix_mmap_size, "matrix-r");

	if (verify) {
		s = (matrix_ptr_t)stress_mmap_populate(NULL, matrix_mmap_size,
			PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (s == MAP_FAILED) {
			pr_fail("%s: matrix allocation failed, out of memory%s, errno=%d (%s)\n",
				args->name, stress_get_memfree_str(),
				errno, strerror(errno));
			goto tidy_r;
		}
		(void)stress_madvise_collapse(s, matrix_mmap_size);
		stress_set_vma_anon_name(r, matrix_mmap_size, "matrix-s");
	}

	/*
	 *  Initialise matrices
	 */
	for (i = 0; i < n; i++) {
		for (j = 0; j < n; j++) {
			a[i][j] = stress_matrix_data(v);
			b[i][j] = stress_matrix_data(v);
			r[i][j] = 0.0;
		}
	}

	ret = EXIT_SUCCESS;

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	do {
		double t;

		t = stress_time_now();
		(void)func(n, a, b, r);
		matrix_metrics[matrix_method].duration += stress_time_now() - t;
		matrix_metrics[matrix_method].count += 1.0;
		stress_bogo_inc(args);

		if (verify) {
			t = stress_time_now();
			(void)func(n, a, b, s);
			matrix_metrics[matrix_method].duration += stress_time_now() - t;
			matrix_metrics[matrix_method].count += 1.0;
			stress_bogo_inc(args);

			if (shim_memcmp(r, s, matrix_size)) {
				pr_fail("%s: %s: data difference between identical matrix computations\n",
					args->name, current_method);
				ret = EXIT_FAILURE;
			}
		}
		if (matrix_method == 0) {
			method_all_index++;
			if (method_all_index >= SIZEOF_ARRAY(matrix_methods))
				method_all_index = 1;
		}
	} while (stress_continue(args));

	mantissa = 1.0;
	exponent = 0;

	/* Dump metrics except for 'all' method */
	for (i = 1, j = 0; i < num_matrix_methods; i++) {
		if (matrix_metrics[i].duration > 0.0) {
			char msg[64];
			int e;
			const double rate = matrix_metrics[i].count / matrix_metrics[i].duration;
			const double f = frexp((double)rate, &e);

			mantissa *= f;
			exponent += e;

			(void)snprintf(msg, sizeof(msg), "%s matrix ops per sec", matrix_methods[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	if (j > 0) {
		double inverse_n = 1.0 / (double)j;
		double geomean = pow(mantissa, inverse_n) *
				 pow(2.0, (double)exponent * inverse_n);
		pr_dbg("%s: %.2f matrix ops per second (geometric mean of per stressor bogo-op rates)\n",
			args->name, geomean);
	}


	if (verify)
		(void)munmap((void *)s, matrix_mmap_size);
tidy_r:
	(void)munmap((void *)r, matrix_mmap_size);
tidy_b:
	(void)munmap((void *)b, matrix_mmap_size);
tidy_a:
	(void)munmap((void *)a, matrix_mmap_size);
tidy_ret:
	return ret;
}

/*
 *  stress_matrix()
 *	stress CPU by doing floating point math ops
 */
static int stress_matrix(stress_args_t *args)
{
	size_t matrix_method = 0;	/* All method */
	size_t matrix_size = DEFAULT_MATRIX_SIZE;
	size_t matrix_yx = 0;
	int rc;

	stress_catch_sigill();

	(void)stress_get_setting("matrix-method", &matrix_method);
	(void)stress_get_setting("matrix-yx", &matrix_yx);

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s' (%s)\n", args->name, matrix_methods[matrix_method].name,
			matrix_yx ? "y by x" : "x by y");

	if (!stress_get_setting("matrix-size", &matrix_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			matrix_size = MAX_MATRIX_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			matrix_size = MIN_MATRIX_SIZE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	rc = stress_matrix_exercise(args, matrix_method, matrix_yx, matrix_size);

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

static const char *stress_matrix_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(matrix_methods)) ? matrix_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_matrix_method, "matrix-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_matrix_method },
	{ OPT_matrix_size,   "matrix-size",   TYPE_ID_SIZE_T, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE, NULL },
	{ OPT_matrix_yx,     "matrix-yx",     TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_matrix_info = {
	.stressor = stress_matrix,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};

#else

static const stress_opt_t opts[] = {
	{ OPT_matrix_method, "matrix-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_unimplemented_method },
	{ OPT_matrix_size,   "matrix-size",   TYPE_ID_SIZE_T, MIN_MATRIX_SIZE, MAX_MATRIX_SIZE, NULL },
	{ OPT_matrix_yx,     "matrix-yx",     TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_matrix_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_CPU_CACHE | CLASS_MEMORY | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "compiler does not support variable length array function arguments"
};
#endif
