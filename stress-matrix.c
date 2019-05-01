/*
 * Copyright (C) 2013-2019 Canonical, Ltd.
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
 * This code is a complete clean re-write of the stress tool by
 * Colin Ian King <colin.king@canonical.com> and attempts to be
 * backwardly compatible with the stress tool by Amos Waterland
 * <apw@rossby.metr.ou.edu> but has more stress tests and more
 * functionality.
 *
 */
#include "stress-ng.h"

static const help_t help[] = {
	{ NULL,	"matrix N",		"start N workers exercising matrix operations" },
	{ NULL,	"matrix-ops N",		"stop after N maxtrix bogo operations" },
	{ NULL,	"matrix-method M",	"specify matrix stress method M, default is all" },
	{ NULL,	"matrix-size N",	"specify the size of the N x N matrix" },
	{ NULL,	"matrix-yx",		"matrix operation is y by x instread of x by y" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_VLA_ARG)

typedef float	matrix_type_t;

/*
 *  the matrix stress test has different classes of maxtrix stressor
 */
typedef void (*stress_matrix_func)(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n]);

typedef struct {
	const char			*name;		/* human readable form of stressor */
	const stress_matrix_func	func[2];	/* method functions, x by y, y by x */
} stress_matrix_method_info_t;

static const stress_matrix_method_info_t matrix_methods[];

static int stress_set_matrix_size(const char *opt)
{
	size_t matrix_size;

	matrix_size = get_uint64(opt);
	check_range("matrix-size", matrix_size,
		MIN_MATRIX_SIZE, MAX_MATRIX_SIZE);
	return set_setting("matrix-size", TYPE_ID_SIZE_T, &matrix_size);
}

static int stress_set_matrix_yx(const char *opt)
{
	size_t matrix_yx = 1;

	(void)opt;

	return set_setting("matrix-yx", TYPE_ID_SIZE_T, &matrix_yx);
}

/*
 *  stress_matrix_xy_prod()
 *	matrix product
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_prod(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * b[k][j];
			}
			if (!g_keep_stressing_flag)
				return;
		}
	}
}

/*
 *  stress_matrix_yx_prod()
 *	matrix product
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_prod(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * b[k][j];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_xy_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_add(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] + b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_yx_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_add(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] + b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_xy_sub()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_sub(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] - b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_xy_sub()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_sub(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {

		register size_t i;
		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] - b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_trans(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],	/* Ignored */
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = a[j][i];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_trans(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],	/* Ignored */
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[j][i];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_mult(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;
	matrix_type_t v = b[0][0];

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = v * a[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_mult(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;
	matrix_type_t v = b[0][0];

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = v * a[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_div(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;
	matrix_type_t v = b[0][0];

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] / v;
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_div(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;
	matrix_type_t v = b[0][0];

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] / v;
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_hadamard(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			r[i][j] = a[i][j] * b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_hadamard(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			r[i][j] = a[i][j] * b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_frobenius(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;
	matrix_type_t sum = 0.0;

	(void)r;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			sum += a[i][j] * b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
	double_put(sum);
}

/*
 *  stress_matrix_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_frobenius(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;
	matrix_type_t sum = 0.0;

	(void)r;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++) {
			sum += a[i][j] * b[i][j];
		}
		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
	double_put(sum);
}

/*
 *  stress_matrix_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_copy(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++)
			r[i][j] = a[i][j];

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_copy(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++)
			r[i][j] = a[i][j];

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_mean(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++)
			r[i][j] = (a[i][j] + b[i][j]) / 2.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_mean(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++)
			r[i][j] = (a[i][j] + b[i][j]) / 2.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_zero(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++)
			r[i][j] = 0.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_zero(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++)
			r[i][j] = 0.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_negate(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++)
			r[i][j] = -a[i][j];

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_negate(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++)
			r[i][j] = -a[i][j];

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_identity(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++)
			r[i][j] = (i == j) ? 1.0 : 0.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_identity(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	register size_t j;

	(void)a;
	(void)b;

	for (j = 0; j < n; j++) {
		register size_t i;

		for (i = 0; i < n; i++)
			r[i][j] = (i == j) ? 1.0 : 0.0;

		if (UNLIKELY(!g_keep_stressing_flag))
			return;
	}
}

/*
 *  stress_matrix_xy_square()
 *	matrix product, r = a x a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_xy_square(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j] += a[i][k] * a[k][j];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_yx_square()
 *	matrix product, r = a x a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_yx_square(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
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
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}


/*
 *  stress_matrix_all()
 *	iterate over all cpu stressors
 */
static void OPTIMIZE3 stress_matrix_xy_all(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	static int i = 1;	/* Skip over stress_matrix_all */

	matrix_methods[i++].func[0](n, a, b, r);
	if (!matrix_methods[i].name)
		i = 1;
}

/*
 *  stress_matrix_all()
 *	iterate over all cpu stressors
 */
static void OPTIMIZE3 stress_matrix_yx_all(
	const size_t n,
	matrix_type_t a[RESTRICT n][n],
	matrix_type_t b[RESTRICT n][n],
	matrix_type_t r[RESTRICT n][n])
{
	static int i = 1;	/* Skip over stress_matrix_all */

	matrix_methods[i++].func[1](n, a, b, r);
	if (!matrix_methods[i].name)
		i = 1;
}


/*
 * Table of cpu stress methods, ordered x by y and y by x
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
	{ NULL,			{ NULL, NULL } }
};

static const stress_matrix_method_info_t *stress_get_matrix_method(
	const char *name)
{
	const stress_matrix_method_info_t *info;

	for (info = matrix_methods; info->name; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("matrix-method", TYPE_ID_STR, name);
			return info;
		}
	}
	return NULL;
}

static void stress_matrix_method_error(void)
{
	const stress_matrix_method_info_t *info;

	(void)fprintf(stderr, "matrix-method must be one of:");
	for (info = matrix_methods; info->name; info++)
		(void)fprintf(stderr, " %s", info->name);
	(void)fprintf(stderr, "\n");
}

/*
 *  stress_set_matrix_method()
 *	set the default matrix stress method
 */
static int stress_set_matrix_method(const char *name)
{
	const stress_matrix_method_info_t *info;

	info = stress_get_matrix_method(name);
	if (info) {
		set_setting("matrix-method", TYPE_ID_STR, name);
		return 0;
	}
	stress_matrix_method_error();

	return -1;
}

static inline size_t round_up(size_t page_size, size_t n)
{
	page_size = (page_size == 0) ? 4096 : page_size;

	return (n + page_size - 1) & (~(page_size -1));
}

static inline int stress_matrix_exercise(
	const args_t *args,
	const stress_matrix_func func,
	const size_t n)
{
	int ret = EXIT_NO_RESOURCE;
	typedef matrix_type_t (*matrix_ptr_t)[n];
	size_t matrix_size = round_up(args->page_size, (sizeof(matrix_type_t) * n * n));

	matrix_ptr_t a, b = NULL, r = NULL;
	register size_t i;
	const matrix_type_t v = 65535 / (matrix_type_t)((uint64_t)~0);
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	a = (matrix_ptr_t)mmap(NULL, matrix_size,
		PROT_READ | PROT_WRITE, flags, -1, 0);
	if (a == MAP_FAILED) {
		pr_fail("matrix allocation");
		goto tidy_ret;
	}
	b = (matrix_ptr_t)mmap(NULL, matrix_size,
		PROT_READ | PROT_WRITE, flags, -1, 0);
	if (b == MAP_FAILED) {
		pr_fail("matrix allocation");
		goto tidy_a;
	}
	r = (matrix_ptr_t)mmap(NULL, matrix_size,
		PROT_READ | PROT_WRITE, flags, -1, 0);
	if (r == MAP_FAILED) {
		pr_fail("matrix allocation");
		goto tidy_b;
	}

	/*
	 *  Initialise matrices
	 */
	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			a[i][j] = (matrix_type_t)mwc64() * v;
			b[i][j] = (matrix_type_t)mwc64() * v;
			r[i][j] = 0.0;
		}
	}

	/*
	 * Normal use case, 100% load, simple spinning on CPU
	 */
	do {
		(void)func(n, a, b, r);
		inc_counter(args);
	} while (keep_stressing());

	ret = EXIT_SUCCESS;

	munmap((void *)r, matrix_size);
tidy_b:
	munmap((void *)b, matrix_size);
tidy_a:
	munmap((void *)a, matrix_size);
tidy_ret:
	return ret;
}

/*
 *  stress_matrix()
 *	stress CPU by doing floating point math ops
 */
static int stress_matrix(const args_t *args)
{
	char *matrix_method_name;
	const stress_matrix_method_info_t *matrix_method;
	stress_matrix_func func;
	size_t matrix_size = 128;
	size_t matrix_yx = 0;

	(void)get_setting("matrix-method", &matrix_method_name);
	(void)get_setting("matrix-yx", &matrix_yx);

	matrix_method = stress_get_matrix_method(matrix_method_name);
	if (!matrix_method) {
		/* Should *never* get here... */
		stress_matrix_method_error();
		return EXIT_FAILURE;
	}

	func = matrix_method->func[matrix_yx];
	if (args->instance == 0)
		pr_dbg("%s using method '%s' (%s)\n", args->name, matrix_method->name,
			matrix_yx ? "y by x" : "x by y");

	if (!get_setting("matrix-size", &matrix_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			matrix_size = MAX_MATRIX_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			matrix_size = MIN_MATRIX_SIZE;
	}

	return stress_matrix_exercise(args, func, matrix_size);
}

static void stress_matrix_set_default(void)
{
	stress_set_matrix_method("all");
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_matrix_method,	stress_set_matrix_method },
	{ OPT_matrix_size,	stress_set_matrix_size },
	{ OPT_matrix_yx,	stress_set_matrix_yx },
	{ 0,			NULL },
};

stressor_info_t stress_matrix_info = {
	.stressor = stress_matrix,
	.set_default = stress_matrix_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};

#else
stressor_info_t stress_matrix_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#endif
