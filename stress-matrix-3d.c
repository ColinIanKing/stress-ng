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
	{ NULL,	"matrix-3d N",		"start N workers exercising 3D matrix operations" },
	{ NULL,	"matrix-3d-ops N",	"stop after N 3D maxtrix bogo operations" },
	{ NULL,	"matrix-3d-method M",	"specify 3D matrix stress method M, default is all" },
	{ NULL,	"matrix-3d-size N",	"specify the size of the N x N x N matrix" },
	{ NULL,	"matrix-3d-zyx",	"matrix operation is z by y by x instread of x by y by z" },
	{ NULL,	NULL,			NULL }
};

#if defined(HAVE_VLA_ARG) &&	\
    !defined(__PCC__)

typedef float	matrix_3d_type_t;

/*
 *  the matrix stress test has different classes of maxtrix stressor
 */
typedef void (*stress_matrix_3d_func)(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n]);

typedef struct {
	const char			*name;		/* human readable form of stressor */
	const stress_matrix_3d_func	func[2];	/* method functions, x by y by z, z by y by x */
} stress_matrix_3d_method_info_t;

static const stress_matrix_3d_method_info_t matrix_3d_methods[];

static int stress_set_matrix_3d_size(const char *opt)
{
	size_t matrix_3d_size;

	matrix_3d_size = get_uint64(opt);
	check_range("matrix-3d-size", matrix_3d_size,
		MIN_MATRIX3D_SIZE, MAX_MATRIX3D_SIZE);
	return set_setting("matrix-3d-size", TYPE_ID_SIZE_T, &matrix_3d_size);
}

static int stress_set_matrix_3d_zyx(const char *opt)
{
	size_t matrix_3d_zyx = 1;

        (void)opt;

        return set_setting("matrix-3d-zyx", TYPE_ID_SIZE_T, &matrix_3d_zyx);
}

/*
 *  stress_matrix_3d_xyz_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_add(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[i][j][k] + b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_zyx_add()
 *	matrix addition
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_add(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = a[i][j][k] + b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_xyz_sub()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_sub(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[i][j][k] - b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_zyx_add()
 *	matrix subtraction
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_sub(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = a[i][j][k] + b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_trans(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],	/* Ignored */
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[k][j][i];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_trans()
 *	matrix transpose
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_trans(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],	/* Ignored */
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)b;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;
	
			for (i = 0; i < n; i++) {
				r[i][j][k] = a[k][j][i];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_mult(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)b;
	matrix_3d_type_t v = b[0][0][0];

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = v * a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_mult()
 *	matrix scalar multiply
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_mult(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)b;
	matrix_3d_type_t v = b[0][0][0];

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = v * a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_div(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)b;
	matrix_3d_type_t v = b[0][0][0];

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[i][j][k] / v;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_div()
 *	matrix scalar divide
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_div(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)b;
	matrix_3d_type_t v = b[0][0][0];

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = a[i][j][k] / v;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_hadamard(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[i][j][k] * b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_hadamard()
 *	matrix hadamard product
 *	(A o B)ij = AijBij
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_hadamard(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = a[i][j][k] * b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_frobenius(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;
	matrix_3d_type_t sum = 0.0;

	(void)r;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				sum += a[i][j][k] * b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
	double_put(sum);
}

/*
 *  stress_matrix_3d_frobenius()
 *	matrix frobenius product
 *	A : B = Sum(AijBij)
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_frobenius(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;
	matrix_3d_type_t sum = 0.0;

	(void)r;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				sum += a[i][j][k] * b[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
	double_put(sum);
}

/*
 *  stress_matrix_3d_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_copy(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_copy()
 *	naive matrix copy, r = a
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_copy(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)b;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_mean(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = (a[i][j][k] + b[i][j][k]) / 2.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_mean(void)
 *	arithmetic mean
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_mean(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = (a[i][j][k] + b[i][j][k]) / 2.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_zero(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = 0.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_zero()
 *	simply zero the result matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_zero(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)a;
	(void)b;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = 0.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_negate(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = -a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_negate()
 *	simply negate the matrix a and put result in r
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_negate(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)a;
	(void)b;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = -a[i][j][k];
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_xyz_identity(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t i;

	(void)a;
	(void)b;

	for (i = 0; i < n; i++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t k;

			for (k = 0; k < n; k++) {
				r[i][j][k] = ((i == j) && (j == k)) ? 1.0 : 0.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_identity()
 *	set r to the identity matrix
 */
static void OPTIMIZE3 TARGET_CLONES stress_matrix_3d_zyx_identity(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	register size_t k;

	(void)a;
	(void)b;

	for (k = 0; k < n; k++) {
		register size_t j;

		for (j = 0; j < n; j++) {
			register size_t i;

			for (i = 0; i < n; i++) {
				r[i][j][k] = ((i == j) && (j == k)) ? 1.0 : 0.0;
			}
			if (UNLIKELY(!g_keep_stressing_flag))
				return;
		}
	}
}

/*
 *  stress_matrix_3d_all()
 *	iterate over all cpu stressors
 */
static void OPTIMIZE3 stress_matrix_3d_xyz_all(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	static int i = 1;	/* Skip over stress_matrix_3d_all */

	matrix_3d_methods[i++].func[0](n, a, b, r);
	if (!matrix_3d_methods[i].name)
		i = 1;
}

/*
 *  stress_matrix_3d_all()
 *	iterate over all cpu stressors
 */
static void OPTIMIZE3 stress_matrix_3d_zyx_all(
	const size_t n,
	matrix_3d_type_t a[RESTRICT n][n][n],
	matrix_3d_type_t b[RESTRICT n][n][n],
	matrix_3d_type_t r[RESTRICT n][n][n])
{
	static int i = 1;	/* Skip over stress_matrix_3d_all */

	matrix_3d_methods[i++].func[1](n, a, b, r);
	if (!matrix_3d_methods[i].name)
		i = 1;
}


/*
 * Table of cpu stress methods, ordered x by y by z and z by y by x
 */
static const stress_matrix_3d_method_info_t matrix_3d_methods[] = {
	{ "all",		{ stress_matrix_3d_xyz_all,	stress_matrix_3d_zyx_all } },/* Special "all" test */

	{ "add",		{ stress_matrix_3d_xyz_add,	stress_matrix_3d_zyx_add } },
	{ "copy",		{ stress_matrix_3d_xyz_copy,	stress_matrix_3d_zyx_copy } },
	{ "div",		{ stress_matrix_3d_xyz_div,	stress_matrix_3d_zyx_div } },
	{ "frobenius",		{ stress_matrix_3d_xyz_frobenius,stress_matrix_3d_zyx_frobenius } },
	{ "hadamard",		{ stress_matrix_3d_xyz_hadamard,	stress_matrix_3d_zyx_hadamard } },
	{ "identity",		{ stress_matrix_3d_xyz_identity,	stress_matrix_3d_zyx_identity } },
	{ "mean",		{ stress_matrix_3d_xyz_mean,	stress_matrix_3d_zyx_mean } },
	{ "mult",		{ stress_matrix_3d_xyz_mult,	stress_matrix_3d_zyx_mult } },
	{ "negate",		{ stress_matrix_3d_xyz_negate,	stress_matrix_3d_zyx_negate } },
	{ "sub",		{ stress_matrix_3d_xyz_sub,	stress_matrix_3d_zyx_sub } },
	{ "trans",		{ stress_matrix_3d_xyz_trans,	stress_matrix_3d_zyx_trans } },
	{ "zero",		{ stress_matrix_3d_xyz_zero,	stress_matrix_3d_zyx_zero } },
	{ NULL,			{ NULL, NULL } }
};

static const stress_matrix_3d_method_info_t *stress_get_matrix_3d_method(
	const char *name)
{
	const stress_matrix_3d_method_info_t *info;

	for (info = matrix_3d_methods; info->name; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("matrix-3d-method", TYPE_ID_STR, name);
			return info;
		}
	}
	return NULL;
}

static void stress_matrix_3d_method_error(void)
{
	const stress_matrix_3d_method_info_t *info;

	(void)fprintf(stderr, "matrix-3d-method must be one of:");
	for (info = matrix_3d_methods; info->name; info++)
		(void)fprintf(stderr, " %s", info->name);
	(void)fprintf(stderr, "\n");
}

/*
 *  stress_set_matrix_3d_method()
 *	set the default matrix stress method
 */
static int stress_set_matrix_3d_method(const char *name)
{
	const stress_matrix_3d_method_info_t *info;

	info = stress_get_matrix_3d_method(name);
	if (info) {
		set_setting("matrix-3d-method", TYPE_ID_STR, name);
		return 0;
	}
	stress_matrix_3d_method_error();

	return -1;
}

static inline size_t round_up(size_t page_size, size_t n)
{
	page_size = (page_size == 0) ? 4096 : page_size;

	return (n + page_size - 1) & (~(page_size -1));
}

static inline int stress_matrix_3d_exercise(
	const args_t *args,
	const stress_matrix_3d_func func,
	const size_t n)
{
	int ret = EXIT_NO_RESOURCE;
	typedef matrix_3d_type_t (*matrix_3d_ptr_t)[n][n];
	size_t matrix_3d_size = round_up(args->page_size, (sizeof(matrix_3d_type_t) * n * n * n));

	matrix_3d_ptr_t a, b = NULL, r = NULL;
	register size_t i;
	const matrix_3d_type_t v = 65535 / (matrix_3d_type_t)((uint64_t)~0);
	int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_POPULATE)
	flags |= MAP_POPULATE;
#endif

	a = (matrix_3d_ptr_t)mmap(NULL, matrix_3d_size,
		PROT_READ | PROT_WRITE, flags, -1, 0);
	if (a == MAP_FAILED) {
		pr_fail("matrix allocation");
		goto tidy_ret;
	}
	b = (matrix_3d_ptr_t)mmap(NULL, matrix_3d_size,
		PROT_READ | PROT_WRITE, flags, -1, 0);
	if (b == MAP_FAILED) {
		pr_fail("matrix allocation");
		goto tidy_a;
	}
	r = (matrix_3d_ptr_t)mmap(NULL, matrix_3d_size,
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
			register size_t k;

			for (k = 0; k < n; k++) {
				a[i][j][k] = (matrix_3d_type_t)mwc64() * v;
				b[i][j][k] = (matrix_3d_type_t)mwc64() * v;
				r[i][j][k] = 0.0;
			}
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

	munmap((void *)r, matrix_3d_size);
tidy_b:
	munmap((void *)b, matrix_3d_size);
tidy_a:
	munmap((void *)a, matrix_3d_size);
tidy_ret:
	return ret;
}

/*
 *  stress_matrix()
 *	stress CPU by doing floating point math ops
 */
static int stress_matrix(const args_t *args)
{
	char *matrix_3d_method_name;
	const stress_matrix_3d_method_info_t *matrix_3d_method;
	stress_matrix_3d_func func;
	size_t matrix_3d_size = 128;
	size_t matrix_3d_yx = 0;

	(void)get_setting("matrix-3d-method", &matrix_3d_method_name);
	(void)get_setting("matrix-3d-zyx", &matrix_3d_yx);

	matrix_3d_method = stress_get_matrix_3d_method(matrix_3d_method_name);
	if (!matrix_3d_method) {
		/* Should *never* get here... */
		stress_matrix_3d_method_error();
		return EXIT_FAILURE;
	}

	func = matrix_3d_method->func[matrix_3d_yx];
	if (args->instance == 0)
		pr_dbg("%s using method '%s' (%s)\n", args->name, matrix_3d_method->name,
			matrix_3d_yx ? "z by y by x" : "x by y by z");

	if (!get_setting("matrix-3d-size", &matrix_3d_size)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			matrix_3d_size = MAX_MATRIX_SIZE;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			matrix_3d_size = MIN_MATRIX_SIZE;
	}

	return stress_matrix_3d_exercise(args, func, matrix_3d_size);
}

static void stress_matrix_3d_set_default(void)
{
	stress_set_matrix_3d_method("all");
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_matrix_3d_method,	stress_set_matrix_3d_method },
	{ OPT_matrix_3d_size,	stress_set_matrix_3d_size },
	{ OPT_matrix_3d_zyx,	stress_set_matrix_3d_zyx },
	{ 0,			NULL }
};

stressor_info_t stress_matrix_3d_info = {
	.stressor = stress_matrix,
	.set_default = stress_matrix_3d_set_default,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#else
stressor_info_t stress_matrix_3d_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE | CLASS_MEMORY,
	.help = help
};
#endif
