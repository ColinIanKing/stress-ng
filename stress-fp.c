/*
 * Copyright (C) 2023-2025 Colin Ian King
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
#include "core-arch.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-madvise.h"
#include "core-mmap.h"
#include "core-put.h"
#include "core-target-clones.h"

#define LOOPS_PER_CALL	(65536)
#define FP_ELEMENTS	(8)

/* Currently division with float 80 on ICC trips SIGFPEs, so disable */
#if defined(__ICC)
#undef HAVE__float80
#endif
#if defined(__OpenBSD__)
#undef HAVE__float128
#endif

#define STRESS_FP_TYPE_LONG_DOUBLE	(0)
#define STRESS_FP_TYPE_DOUBLE		(1)
#define STRESS_FP_TYPE_FLOAT		(2)
#define STRESS_FP_TYPE_FLOAT16		(4)
#define STRESS_FP_TYPE_FLOAT32		(5)
#define STRESS_FP_TYPE_FLOAT64		(6)
#define STRESS_FP_TYPE_FLOAT80		(7)
#define STRESS_FP_TYPE_FLOAT128		(8)
#define STRESS_FP_TYPE_IBM128		(9)
#define STRESS_FP_TYPE_BF16		(10)
#define STRESS_FP_TYPE_ALL		(11)

static const stress_help_t help[] = {
	{ NULL,	"fp N",	 	"start N workers performing floating point math ops" },
	{ NULL,	"fp-method M",	"select the floating point method to operate with" },
	{ NULL,	"fp-ops N",	"stop after N floating point math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

typedef struct {
	struct {
		long double r_init;	/* initialization value for r */
		long double r[2];	/* result of computation */
		long double add;	/* value to add */
		long double add_rev;	/* value to add to revert back */
		long double mul;	/* value to multiply */
		long double mul_rev;	/* value to multiply to revert back */
	} ld;
	struct {
		double r_init;		/* initialization value for r */
		double r[2];		/* result of computation */
		double add;		/* value to add */
		double add_rev;		/* value to add to revert back */
		double mul;		/* value to multiply */
		double mul_rev;		/* value to multiply to revert back */
	} d;
	struct {
		float r_init;		/* initialization value for r */
		float r[2];		/* result of computation */
		float add;		/* value to add */
		float add_rev;		/* value to add to revert back */
		float mul;		/* value to multiply */
		float mul_rev;		/* value to multiply to revert back */
	} f;
#if defined(HAVE__bf16)
	struct {
		__bf16 r_init;		/* initialization value for r */
		__bf16 r[2];		/* result of computation */
		__bf16 add;		/* value to add */
		__bf16 add_rev;		/* value to add to revert back */
		__bf16 mul;		/* value to multiply */
		__bf16 mul_rev;		/* value to multiply to revert back */
	} bf16;
#endif
#if defined(HAVE_Float16)
	struct {
		_Float16 r_init;	/* initialization value for r */
		_Float16 r[2];		/* result of computation */
		_Float16 add;		/* value to add */
		_Float16 add_rev;	/* value to add to revert back */
		_Float16 mul;		/* value to multiply */
		_Float16 mul_rev;	/* value to multiply to revert back */
	} f16;
#endif
#if defined(HAVE_Float32)
	struct {
		_Float32 r_init;	/* initialization value for r */
		_Float32 r[2];		/* result of computation */
		_Float32 add;		/* value to add */
		_Float32 add_rev;	/* value to add to revert back */
		_Float32 mul;		/* value to multiply */
		_Float32 mul_rev;	/* value to multiply to revert back */
	} f32;
#endif
#if defined(HAVE_Float64)
	struct {
		_Float64 r_init;	/* initialization value for r */
		_Float64 r[2];		/* result of computation */
		_Float64 add;		/* value to add */
		_Float64 add_rev;	/* value to add to revert back */
		_Float64 mul;		/* value to multiply */
		_Float64 mul_rev;	/* value to multiply to revert back */
	} f64;
#endif
#if defined(HAVE__float80)
	struct {
		__float80 r_init;	/* initialization value for r */
		__float80 r[2];		/* result of computation */
		__float80 add;		/* value to add */
		__float80 add_rev;	/* value to add to revert back */
		__float80 mul;		/* value to multiply */
		__float80 mul_rev;	/* value to multiply to revert back */
	} f80;
#endif
#if defined(HAVE__float128)
	struct {
		__float128 r_init;	/* initialization value for r */
		__float128 r[2];	/* result of computation */
		__float128 add;		/* value to add */
		__float128 add_rev;	/* value to add to revert back */
		__float128 mul;		/* value to multiply */
		__float128 mul_rev;	/* value to multiply to revert back */
	} f128;
#elif defined(HAVE_Float128)
	struct {
		_Float128 r_init;	/* initialization value for r */
		_Float128 r[2];	/* result of computation */
		_Float128 add;		/* value to add */
		_Float128 add_rev;	/* value to add to revert back */
		_Float128 mul;		/* value to multiply */
		_Float128 mul_rev;	/* value to multiply to revert back */
	} f128;
#endif
#if defined(HAVE__ibm128)
	struct {
		__ibm128 r_init;	/* initialization value for r */
		__ibm128 r[2];	/* result of computation */
		__ibm128 add;		/* value to add */
		__ibm128 add_rev;	/* value to add to revert back */
		__ibm128 mul;		/* value to multiply */
		__ibm128 mul_rev;	/* value to multiply to revert back */
	} ibm128;
#endif
} fp_data_t;

typedef double (*stress_fp_func_t)(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx);

static double stress_fp_all(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx);

#define STRESS_FP_ADD(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	fp_data_t *fp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[idx] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		fp_data[0].field.r[idx] += fp_data[0].field.add;	\
		fp_data[0].field.r[idx] += fp_data[0].field.add_rev;	\
		fp_data[1].field.r[idx] += fp_data[1].field.add;	\
		fp_data[1].field.r[idx] += fp_data[1].field.add_rev;	\
		fp_data[2].field.r[idx] += fp_data[2].field.add;	\
		fp_data[2].field.r[idx] += fp_data[2].field.add_rev;	\
		fp_data[3].field.r[idx] += fp_data[3].field.add;	\
		fp_data[3].field.r[idx] += fp_data[3].field.add_rev;	\
		fp_data[4].field.r[idx] += fp_data[4].field.add;	\
		fp_data[4].field.r[idx] += fp_data[4].field.add_rev;	\
		fp_data[5].field.r[idx] += fp_data[5].field.add;	\
		fp_data[5].field.r[idx] += fp_data[5].field.add_rev;	\
		fp_data[6].field.r[idx] += fp_data[6].field.add;	\
		fp_data[6].field.r[idx] += fp_data[6].field.add_rev;	\
		fp_data[7].field.r[idx] += fp_data[7].field.add;	\
		fp_data[7].field.r[idx] += fp_data[7].field.add_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_FP_SUB(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	fp_data_t *fp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[idx] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		fp_data[0].field.r[idx] -= fp_data[0].field.add;	\
		fp_data[0].field.r[idx] -= fp_data[0].field.add_rev;	\
		fp_data[1].field.r[idx] -= fp_data[1].field.add;	\
		fp_data[1].field.r[idx] -= fp_data[1].field.add_rev;	\
		fp_data[2].field.r[idx] -= fp_data[2].field.add;	\
		fp_data[2].field.r[idx] -= fp_data[2].field.add_rev;	\
		fp_data[3].field.r[idx] -= fp_data[3].field.add;	\
		fp_data[3].field.r[idx] -= fp_data[3].field.add_rev;	\
		fp_data[4].field.r[idx] -= fp_data[4].field.add;	\
		fp_data[4].field.r[idx] -= fp_data[4].field.add_rev;	\
		fp_data[5].field.r[idx] -= fp_data[5].field.add;	\
		fp_data[5].field.r[idx] -= fp_data[5].field.add_rev;	\
		fp_data[6].field.r[idx] -= fp_data[6].field.add;	\
		fp_data[6].field.r[idx] -= fp_data[6].field.add_rev;	\
		fp_data[7].field.r[idx] -= fp_data[7].field.add;	\
		fp_data[7].field.r[idx] -= fp_data[7].field.add_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_FP_MUL(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	fp_data_t *fp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[idx] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		fp_data[0].field.r[idx] *= fp_data[0].field.mul;	\
		fp_data[0].field.r[idx] *= fp_data[0].field.mul_rev;	\
		fp_data[1].field.r[idx] *= fp_data[1].field.mul;	\
		fp_data[1].field.r[idx] *= fp_data[1].field.mul_rev;	\
		fp_data[2].field.r[idx] *= fp_data[2].field.mul;	\
		fp_data[2].field.r[idx] *= fp_data[2].field.mul_rev;	\
		fp_data[3].field.r[idx] *= fp_data[3].field.mul;	\
		fp_data[3].field.r[idx] *= fp_data[3].field.mul_rev;	\
		fp_data[4].field.r[idx] *= fp_data[4].field.mul;	\
		fp_data[4].field.r[idx] *= fp_data[4].field.mul_rev;	\
		fp_data[5].field.r[idx] *= fp_data[5].field.mul;	\
		fp_data[5].field.r[idx] *= fp_data[5].field.mul_rev;	\
		fp_data[6].field.r[idx] *= fp_data[6].field.mul;	\
		fp_data[6].field.r[idx] *= fp_data[6].field.mul_rev;	\
		fp_data[7].field.r[idx] *= fp_data[7].field.mul;	\
		fp_data[7].field.r[idx] *= fp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_FP_DIV(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	stress_args_t *args,						\
	fp_data_t *fp_data,						\
	const int idx)							\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[idx] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; LIKELY(stress_continue_flag() && (i < loops)); i++) {\
		fp_data[0].field.r[idx] /= fp_data[0].field.mul;	\
		fp_data[0].field.r[idx] /= fp_data[0].field.mul_rev;	\
		fp_data[1].field.r[idx] /= fp_data[1].field.mul;	\
		fp_data[1].field.r[idx] /= fp_data[1].field.mul_rev;	\
		fp_data[2].field.r[idx] /= fp_data[2].field.mul;	\
		fp_data[2].field.r[idx] /= fp_data[2].field.mul_rev;	\
		fp_data[3].field.r[idx] /= fp_data[3].field.mul;	\
		fp_data[3].field.r[idx] /= fp_data[3].field.mul_rev;	\
		fp_data[4].field.r[idx] /= fp_data[4].field.mul;	\
		fp_data[4].field.r[idx] /= fp_data[4].field.mul_rev;	\
		fp_data[5].field.r[idx] /= fp_data[5].field.mul;	\
		fp_data[5].field.r[idx] /= fp_data[5].field.mul_rev;	\
		fp_data[6].field.r[idx] /= fp_data[6].field.mul;	\
		fp_data[6].field.r[idx] /= fp_data[6].field.mul_rev;	\
		fp_data[7].field.r[idx] /= fp_data[7].field.mul;	\
		fp_data[7].field.r[idx] /= fp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

STRESS_FP_ADD(ld, stress_fp_ldouble_add, true)
STRESS_FP_ADD(ld, stress_fp_ldouble_sub, true)
STRESS_FP_MUL(ld, stress_fp_ldouble_mul, true)
STRESS_FP_DIV(ld, stress_fp_ldouble_div, true)

STRESS_FP_ADD(d, stress_fp_double_add, true)
STRESS_FP_ADD(d, stress_fp_double_sub, true)
STRESS_FP_MUL(d, stress_fp_double_mul, true)
STRESS_FP_DIV(d, stress_fp_double_div, true)

STRESS_FP_ADD(f, stress_fp_float_add, true)
STRESS_FP_ADD(f, stress_fp_float_sub, true)
STRESS_FP_MUL(f, stress_fp_float_mul, true)
STRESS_FP_DIV(f, stress_fp_float_div, true)

#if defined(HAVE__bf16)
STRESS_FP_ADD(bf16, stress_fp_bf16_add, false)
STRESS_FP_ADD(bf16, stress_fp_bf16_sub, false)
STRESS_FP_MUL(bf16, stress_fp_bf16_mul, false)
STRESS_FP_DIV(bf16, stress_fp_bf16_div, false)
#endif

#if defined(HAVE_Float16)
STRESS_FP_ADD(f16, stress_fp_float16_add, false)
STRESS_FP_ADD(f16, stress_fp_float16_sub, false)
STRESS_FP_MUL(f16, stress_fp_float16_mul, false)
STRESS_FP_DIV(f16, stress_fp_float16_div, false)
#endif

#if defined(HAVE_Float32)
STRESS_FP_ADD(f32, stress_fp_float32_add, false)
STRESS_FP_ADD(f32, stress_fp_float32_sub, false)
STRESS_FP_MUL(f32, stress_fp_float32_mul, false)
STRESS_FP_DIV(f32, stress_fp_float32_div, false)
#endif

#if defined(HAVE_Float64)
STRESS_FP_ADD(f64, stress_fp_float64_add, false)
STRESS_FP_ADD(f64, stress_fp_float64_sub, false)
STRESS_FP_MUL(f64, stress_fp_float64_mul, false)
STRESS_FP_DIV(f64, stress_fp_float64_div, false)
#endif

#if defined(HAVE__float80)
STRESS_FP_ADD(f80, stress_fp_float80_add, false)
STRESS_FP_ADD(f80, stress_fp_float80_sub, false)
STRESS_FP_MUL(f80, stress_fp_float80_mul, false)
STRESS_FP_DIV(f80, stress_fp_float80_div, false)
#endif

#if defined(HAVE__float128) || 	\
    defined(HAVE_Float128)
STRESS_FP_ADD(f128, stress_fp_float128_add, false)
STRESS_FP_ADD(f128, stress_fp_float128_sub, false)
STRESS_FP_MUL(f128, stress_fp_float128_mul, false)
STRESS_FP_DIV(f128, stress_fp_float128_div, false)
#endif

#if defined(HAVE__ibm128)
STRESS_FP_ADD(ibm128, stress_fp_ibm128_add, false)
STRESS_FP_ADD(ibm128, stress_fp_ibm128_sub, false)
STRESS_FP_MUL(ibm128, stress_fp_ibm128_mul, false)
STRESS_FP_DIV(ibm128, stress_fp_ibm128_div, false)
#endif

typedef struct {
	const char *name;
	const char *description;
	const stress_fp_func_t	fp_func;
	const int fp_type;
} stress_fp_funcs_t;

static const stress_fp_funcs_t stress_fp_funcs[] = {
	{ "all",		"all fp methods",	stress_fp_all,		STRESS_FP_TYPE_ALL },

#if defined(HAVE__float128) ||	\
    defined(HAVE_Float128)
	{ "float128add",	"float128 add",		stress_fp_float128_add,	STRESS_FP_TYPE_FLOAT128 },
#endif
#if defined(HAVE__ibm128)
	{ "ibm128add",		"ibm128 add",		stress_fp_ibm128_add,	STRESS_FP_TYPE_IBM128 },
#endif
#if defined(HAVE__float80)
	{ "float80add",		"float80 add",		stress_fp_float80_add,	STRESS_FP_TYPE_FLOAT80 },
#endif
#if defined(HAVE_Float64)
	{ "float64add",		"float64 add",		stress_fp_float64_add,	STRESS_FP_TYPE_FLOAT64 },
#endif
#if defined(HAVE_Float32)
	{ "float32add",		"float32 add",		stress_fp_float32_add,	STRESS_FP_TYPE_FLOAT32 },
#endif
#if defined(HAVE__bf16)
	{ "bf16add",		"bf16 add",		stress_fp_bf16_add,	STRESS_FP_TYPE_BF16 },
#endif
#if defined(HAVE_Float16)
	{ "float16add",		"float16 add",		stress_fp_float16_add,	STRESS_FP_TYPE_FLOAT16 },
#endif
	{ "floatadd",		"float add",		stress_fp_float_add,	STRESS_FP_TYPE_FLOAT },
	{ "doubleadd",		"double add",		stress_fp_double_add,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoubleadd",		"long double add",	stress_fp_ldouble_add,	STRESS_FP_TYPE_LONG_DOUBLE },

#if defined(HAVE__float128) ||	\
    defined(HAVE_Float128)
	{ "float128sub",	"float128 subtract",	stress_fp_float128_sub,	STRESS_FP_TYPE_FLOAT128 },
#endif
#if defined(HAVE__ibm128)
	{ "ibm128sub",		"ibm128 subtract",	stress_fp_ibm128_sub,	STRESS_FP_TYPE_IBM128 },
#endif
#if defined(HAVE__float80)
	{ "float80sub",		"float80 subtract",	stress_fp_float80_sub,	STRESS_FP_TYPE_FLOAT80 },
#endif
#if defined(HAVE_Float64)
	{ "float64sub",		"float64 subtract",	stress_fp_float64_sub,	STRESS_FP_TYPE_FLOAT64 },
#endif
#if defined(HAVE_Float32)
	{ "float32sub",		"float32 subtract",	stress_fp_float32_sub,	STRESS_FP_TYPE_FLOAT32 },
#endif
#if defined(HAVE__bf16)
	{ "bf16sub",		"bf16 subtract",	stress_fp_bf16_sub,	STRESS_FP_TYPE_BF16 },
#endif
#if defined(HAVE_Float16)
	{ "float16sub",		"float16 subtract",	stress_fp_float16_sub,	STRESS_FP_TYPE_FLOAT16 },
#endif
	{ "floatsub",		"float subtract",	stress_fp_float_sub,	STRESS_FP_TYPE_FLOAT },
	{ "doublesub",		"double subtract",	stress_fp_double_sub,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublesub",		"long double subtract",	stress_fp_ldouble_sub,	STRESS_FP_TYPE_LONG_DOUBLE },

#if defined(HAVE__float128) ||	\
    defined(HAVE_Float128)
	{ "float128mul",	"float128 multiply",	stress_fp_float128_mul,	STRESS_FP_TYPE_FLOAT128 },
#endif
#if defined(HAVE__ibm128)
	{ "ibm128mul",		"ibm128 multiply",	stress_fp_ibm128_mul,	STRESS_FP_TYPE_IBM128 },
#endif
#if defined(HAVE__float80)
	{ "float80mul",		"float80 multiply",	stress_fp_float80_mul,	STRESS_FP_TYPE_FLOAT80 },
#endif
#if defined(HAVE_Float64)
	{ "float64mul",		"float64 multiply",	stress_fp_float64_mul,	STRESS_FP_TYPE_FLOAT64 },
#endif
#if defined(HAVE_Float32)
	{ "float32mul",		"float32 multiply",	stress_fp_float32_mul,	STRESS_FP_TYPE_FLOAT32 },
#endif
#if defined(HAVE__bf16)
	{ "bf16mul",		"bf16 multiply",	stress_fp_bf16_mul,	STRESS_FP_TYPE_BF16 },
#endif
#if defined(HAVE_Float16)
	{ "float16mul",		"float16 multiply",	stress_fp_float16_mul,	STRESS_FP_TYPE_FLOAT16 },
#endif
	{ "floatmul",		"float multiply",	stress_fp_float_mul,	STRESS_FP_TYPE_FLOAT },
	{ "doublemul",		"double multiply",	stress_fp_double_mul,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublemul",		"long double multiply",	stress_fp_ldouble_mul,	STRESS_FP_TYPE_LONG_DOUBLE },

#if defined(HAVE__float128) || 	\
    defined(HAVE_Float128)
	{ "float128div",	"float128 divide",	stress_fp_float128_div,	STRESS_FP_TYPE_FLOAT128 },
#endif
#if defined(HAVE__ibm128)
	{ "ibm128div",		"ibm128 divide",	stress_fp_ibm128_div,	STRESS_FP_TYPE_IBM128 },
#endif
#if defined(HAVE__float80)
	{ "float80div",		"float80 divide",	stress_fp_float80_div,	STRESS_FP_TYPE_FLOAT80 },
#endif
#if defined(HAVE_Float64)
	{ "float64div",		"float64 divide",	stress_fp_float64_div,	STRESS_FP_TYPE_FLOAT64 },
#endif
#if defined(HAVE_Float32)
	{ "float32div",		"float32 divide",	stress_fp_float32_div,	STRESS_FP_TYPE_FLOAT32 },
#endif
#if defined(HAVE__bf16)
	{ "bf16div",		"bf16 divide",		stress_fp_bf16_div,	STRESS_FP_TYPE_BF16 },
#endif
#if defined(HAVE_Float16)
	{ "float16div",		"float16 divide",	stress_fp_float16_div,	STRESS_FP_TYPE_FLOAT16 },
#endif
	{ "floatdiv",		"float divide",		stress_fp_float_div,	STRESS_FP_TYPE_FLOAT },
	{ "doublediv",		"double divide",	stress_fp_double_div,	STRESS_FP_TYPE_DOUBLE },
	{ "ldoublediv",		"long double divide",	stress_fp_ldouble_div,	STRESS_FP_TYPE_LONG_DOUBLE },
};

static stress_metrics_t stress_fp_metrics[SIZEOF_ARRAY(stress_fp_funcs)];

#define STRESS_NUM_FP_FUNCS	(SIZEOF_ARRAY(stress_fp_funcs))

typedef struct {
	const int fp_type;
	const char *fp_description;
} fp_type_map_t;

static const fp_type_map_t fp_type_map[] = {
	{ STRESS_FP_TYPE_LONG_DOUBLE,	"long double" },
	{ STRESS_FP_TYPE_DOUBLE,	"double" },
	{ STRESS_FP_TYPE_FLOAT,		"float" },
	{ STRESS_FP_TYPE_BF16,		"bf16" },
	{ STRESS_FP_TYPE_FLOAT16,	"float16" },
	{ STRESS_FP_TYPE_FLOAT32,	"float32" },
	{ STRESS_FP_TYPE_FLOAT64,	"float64" },
	{ STRESS_FP_TYPE_FLOAT80,	"float80" },
	{ STRESS_FP_TYPE_FLOAT128,	"float128" },
	{ STRESS_FP_TYPE_ALL,		"all" },
};

static const char * PURE stress_fp_type(const int fp_type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(fp_type_map); i++) {
		if (fp_type == fp_type_map[i].fp_type)
			return fp_type_map[i].fp_description;
	}
	return "unknown";
}

static int stress_fp_call_method(
	stress_args_t *args,
	fp_data_t *fp_data,
	const size_t method,
	const bool verify)
{
	double dt;
	const stress_fp_funcs_t *func = &stress_fp_funcs[method];
	stress_metrics_t *metrics = &stress_fp_metrics[method];

	dt = func->fp_func(args, fp_data, 0);
	metrics->duration += dt;
	metrics->count += (FP_ELEMENTS * LOOPS_PER_CALL);

	if ((method > 0) && (method < STRESS_NUM_FP_FUNCS && verify)) {
		register size_t i;
		const int fp_type = stress_fp_funcs[method].fp_type;
		const char *method_name = stress_fp_funcs[method].name;
		const char *fp_description = stress_fp_type(fp_type);

		dt = func->fp_func(args, fp_data, 1);
		if (dt < 0.0)
			return EXIT_FAILURE;
		metrics->duration += dt;
		metrics->count += (FP_ELEMENTS * LOOPS_PER_CALL);

		/*
		 *  a SIGALRM during 2nd computation pre-verification can
		 *  cause long doubles on some arches to abort early, so
		 *  don't verify these results
		 */
		if (UNLIKELY(!stress_continue_flag()))
			return EXIT_SUCCESS;

		for (i = 0; i < FP_ELEMENTS; i++) {
			long double r0, r1;
			int ret;

			switch (fp_type) {
			case STRESS_FP_TYPE_LONG_DOUBLE:
				ret = shim_memcmp(&fp_data[i].ld.r[0], &fp_data[i].ld.r[1], sizeof(fp_data[i].ld.r[0]));
				r0 = (long double)fp_data[i].ld.r[0];
				r1 = (long double)fp_data[i].ld.r[1];
				break;
			case STRESS_FP_TYPE_DOUBLE:
				ret = shim_memcmp(&fp_data[i].d.r[0], &fp_data[i].d.r[1], sizeof(fp_data[i].d.r[0]));
				r0 = (long double)fp_data[i].d.r[0];
				r1 = (long double)fp_data[i].d.r[1];
				break;
			case STRESS_FP_TYPE_FLOAT:
				ret = shim_memcmp(&fp_data[i].f.r[0], &fp_data[i].f.r[1], sizeof(fp_data[i].f.r[0]));
				r0 = (long double)fp_data[i].f.r[0];
				r1 = (long double)fp_data[i].f.r[1];
				break;
#if defined(HAVE_Float32)
			case STRESS_FP_TYPE_FLOAT32:
				ret = shim_memcmp(&fp_data[i].f32.r[0], &fp_data[i].f32.r[1], sizeof(fp_data[i].f32.r[0]));
				r0 = (long double)fp_data[i].f32.r[0];
				r1 = (long double)fp_data[i].f32.r[1];
				break;
#endif
#if defined(HAVE_Float64)
			case STRESS_FP_TYPE_FLOAT64:
				ret = shim_memcmp(&fp_data[i].f64.r[0], &fp_data[i].f64.r[1], sizeof(fp_data[i].f64.r[0]));
				r0 = (long double)fp_data[i].f64.r[0];
				r1 = (long double)fp_data[i].f64.r[1];
				break;
#endif
#if defined(HAVE__float80)
			case STRESS_FP_TYPE_FLOAT80:
				ret = shim_memcmp(&fp_data[i].f80.r[0], &fp_data[i].f80.r[1], sizeof(fp_data[i].f80.r[0]));
				r0 = (long double)fp_data[i].f80.r[0];
				r1 = (long double)fp_data[i].f80.r[1];
				break;
#endif
#if defined(HAVE__float128) ||	\
    defined(HAVE_Float128)
			case STRESS_FP_TYPE_FLOAT128:
				ret = shim_memcmp(&fp_data[i].f128.r[0], &fp_data[i].f128.r[1], sizeof(fp_data[i].f128.r[0]));
				r0 = (long double)fp_data[i].f128.r[0];
				r1 = (long double)fp_data[i].f128.r[1];
				break;
#endif
			default:
				/* Should never happen! */
				return EXIT_SUCCESS;
			}
			if (ret) {
				pr_fail("%s %s %s verification failure on element %zd, got %Lf, expected %Lf\n",
					args->name, fp_description, method_name, i, r0, r1);
				return EXIT_FAILURE;
			}
		}
	}
	return EXIT_SUCCESS;
}

static double stress_fp_all(
	stress_args_t *args,
	fp_data_t *fp_data,
	const int idx)
{
	size_t i;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	(void)idx;

	for (i = 1; i < STRESS_NUM_FP_FUNCS; i++) {
		if (stress_fp_call_method(args, fp_data, i, verify) == EXIT_FAILURE)
			return -1.0;
	}
	return 0.0;
}

static int stress_fp(stress_args_t *args)
{
	size_t i, mmap_size;
	fp_data_t *fp_data;
	size_t fp_method = 0;	/* "all" */
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	int rc = EXIT_SUCCESS;

	stress_catch_sigill();

	mmap_size = FP_ELEMENTS * sizeof(*fp_data);
	fp_data = (fp_data_t *)stress_mmap_populate(NULL, mmap_size,
			PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (fp_data == MAP_FAILED) {
		pr_inf_skip("%s: failed to mmap %d floating point elements%s, skipping stressor\n",
			args->name, FP_ELEMENTS, stress_get_memfree_str());
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(fp_data, mmap_size, "fp-data");
	(void)stress_madvise_mergeable(fp_data, mmap_size);

	(void)stress_get_setting("fp-method", &fp_method);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < SIZEOF_ARRAY(stress_fp_metrics); i++) {
		stress_fp_metrics[i].duration = 0.0;
		stress_fp_metrics[i].count = 0.0;
	}

	for (i = 0; i < FP_ELEMENTS; i++) {
		long double ld;
		uint32_t r;

		r = stress_mwc32();
		ld = (long double)i + (long double)r / ((long double)(1ULL << 38));
		fp_data[i].ld.r_init = ld;
		fp_data[i].ld.r[0] = ld;
		fp_data[i].ld.r[1] = ld;

		fp_data[i].d.r_init = (double)ld;
		fp_data[i].d.r[0] = (double)ld;
		fp_data[i].d.r[1] = (double)ld;

		fp_data[i].f.r_init = (float)ld;
		fp_data[i].f.r[0] = (float)ld;
		fp_data[i].f.r[1] = (float)ld;

#if defined(HAVE__bf16)
		fp_data[i].bf16.r_init = (__bf16)ld;
		fp_data[i].bf16.r[0] = (__bf16)ld;
		fp_data[i].bf16.r[1] = (__bf16)ld;
#endif

#if defined(HAVE_Float16)
		fp_data[i].f16.r_init = (_Float16)ld;
		fp_data[i].f16.r[0] = (_Float16)ld;
		fp_data[i].f16.r[1] = (_Float16)ld;
#endif

#if defined(HAVE_Float32)
		fp_data[i].f32.r_init = (_Float32)ld;
		fp_data[i].f32.r[0] = (_Float32)ld;
		fp_data[i].f32.r[1] = (_Float32)ld;
#endif

#if defined(HAVE_Float64)
		fp_data[i].f64.r_init = (_Float64)ld;
		fp_data[i].f64.r[0] = (_Float64)ld;
		fp_data[i].f64.r[1] = (_Float64)ld;
#endif

#if defined(HAVE__float80)
		fp_data[i].f80.r_init = (__float80)ld;
		fp_data[i].f80.r[0] = (__float80)ld;
		fp_data[i].f80.r[1] = (__float80)ld;
#endif

#if defined(HAVE__float128)
		fp_data[i].f128.r_init = (__float128)ld;
		fp_data[i].f128.r[0] = (__float128)ld;
		fp_data[i].f128.r[1] = (__float128)ld;
#elif defined(HAVE_Float128)
		fp_data[i].f128.r_init = (_Float128)ld;
		fp_data[i].f128.r[0] = (_Float128)ld;
		fp_data[i].f128.r[1] = (_Float128)ld;
#endif

		r = stress_mwc32();
		ld = (long double)r / ((long double)(1ULL << 31));
		fp_data[i].ld.add = ld;
		fp_data[i].d.add = (double)ld;
		fp_data[i].f.add = (float)ld;
#if defined(HAVE__bf16)
		fp_data[i].bf16.add = (__bf16)ld;
#endif
#if defined(HAVE_Float16)
		fp_data[i].f16.add = (_Float16)ld;
#endif
#if defined(HAVE_Float32)
		fp_data[i].f32.add = (_Float32)ld;
#endif
#if defined(HAVE_Float64)
		fp_data[i].f64.add = (_Float64)ld;
#endif
#if defined(HAVE__float80)
		fp_data[i].f80.add = (__float80)ld;
#endif
#if defined(HAVE__float128)
		fp_data[i].f128.add = (__float128)ld;
#elif defined(HAVE_Float128)
		fp_data[i].f128.add = (_Float128)ld;
#endif

		ld = -(ld * 0.992);
		fp_data[i].ld.add_rev = ld;
		fp_data[i].d.add_rev = (double)ld;
		fp_data[i].f.add_rev = (float)ld;
#if defined(HAVE__bf16)
		fp_data[i].bf16.add_rev = (__bf16)ld;
#endif
#if defined(HAVE_Float16)
		fp_data[i].f16.add_rev = (_Float16)ld;
#endif
#if defined(HAVE_Float32)
		fp_data[i].f32.add_rev = (_Float32)ld;
#endif
#if defined(HAVE_Float64)
		fp_data[i].f64.add_rev = (_Float64)ld;
#endif
#if defined(HAVE__float80)
		fp_data[i].f80.add_rev = (__float80)ld;
#endif
#if defined(HAVE__float128)
		fp_data[i].f128.add_rev = (__float128)ld;
#elif defined(HAVE_Float128)
		fp_data[i].f128.add_rev = (_Float128)ld;
#endif

		r = stress_mwc32();
		ld = (long double)i + (long double)r / ((long double)(1ULL << 36));
		fp_data[i].ld.mul = ld;
		fp_data[i].d.mul = (double)ld;
		fp_data[i].f.mul = (float)ld;
#if defined(HAVE__bf16)
		fp_data[i].bf16.mul = (__bf16)ld;
#endif
#if defined(HAVE_Float16)
		fp_data[i].f16.mul = (_Float16)ld;
#endif
#if defined(HAVE_Float32)
		fp_data[i].f32.mul = (_Float32)ld;
#endif
#if defined(HAVE_Float64)
		fp_data[i].f64.mul = (_Float64)ld;
#endif
#if defined(HAVE__float80)
		fp_data[i].f80.mul = (__float80)ld;
#endif
#if defined(HAVE__float128)
		fp_data[i].f128.mul = (__float128)ld;
#elif defined(HAVE_Float128)
		fp_data[i].f128.mul = (_Float128)ld;
#endif

		ld = 0.9995 / ld;
		fp_data[i].ld.mul_rev = ld;
		fp_data[i].d.mul_rev = (double)ld;
		fp_data[i].f.mul_rev = (float)ld;
#if defined(HAVE_Float32)
		fp_data[i].f32.mul_rev = (_Float32)ld;
#endif
#if defined(HAVE_Float64)
		fp_data[i].f64.mul_rev = (_Float64)ld;
#endif
#if defined(HAVE__float80)
		fp_data[i].f80.mul_rev = (__float80)ld;
#endif
#if defined(HAVE__float128)
		fp_data[i].f128.mul_rev = (__float128)ld;
#elif defined(HAVE_Float128)
		fp_data[i].f128.mul_rev = (_Float128)ld;
#endif
	}

	do {
		if (stress_fp_call_method(args, fp_data, fp_method, verify) == EXIT_FAILURE) {
			rc = EXIT_FAILURE;
			break;
		}
	} while (stress_continue(args));

	for (i = 1; i < STRESS_NUM_FP_FUNCS; i++) {
		const double count = stress_fp_metrics[i].count;
		const double duration = stress_fp_metrics[i].duration;
		if ((duration > 0.0) && (count > 0.0)) {
			char msg[64];
			const double rate = count / duration;

			(void)snprintf(msg, sizeof(msg), "Mfp-ops per sec, %-20s", stress_fp_funcs[i].description);
			stress_metrics_set(args, i - 1, msg,
				rate / 1000000.0, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)fp_data, mmap_size);

	return rc;
}

static const char *stress_fp_method(const size_t i)
{
	return (i < STRESS_NUM_FP_FUNCS) ? stress_fp_funcs[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_fp_method, "fp-method", TYPE_ID_SIZE_T_METHOD, 0, 1, stress_fp_method },
	END_OPT,
};

const stressor_info_t stress_fp_info = {
	.stressor = stress_fp,
	.classifier = CLASS_CPU | CLASS_FP | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
