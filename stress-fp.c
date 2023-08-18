// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C)      2023 Colin Ian King
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-put.h"
#include "core-target-clones.h"

#define LOOPS_PER_CALL	(65536)
#define FP_ELEMENTS	(8)

/* Currently division with float 80 on ICC trips SIGFPEs, so disable */
#if defined(__ICC)
#undef HAVE_FLOAT80
#endif
#if defined(__OpenBSD__)
#undef HAVE_FLOAT128
#endif

#define STRESS_FP_TYPE_LONG_DOUBLE	(0)
#define STRESS_FP_TYPE_DOUBLE		(1)
#define STRESS_FP_TYPE_FLOAT		(2)
#define STRESS_FP_TYPE_FLOAT32		(3)
#define STRESS_FP_TYPE_FLOAT64		(4)
#define STRESS_FP_TYPE_FLOAT80		(5)
#define STRESS_FP_TYPE_FLOAT128		(6)
#define STRESS_FP_TYPE_ALL		(7)

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
#if defined(HAVE_FLOAT32)
	struct {
		_Float32 r_init;	/* initialization value for r */
		_Float32 r[2];		/* result of computation */
		_Float32 add;		/* value to add */
		_Float32 add_rev;	/* value to add to revert back */
		_Float32 mul;		/* value to multiply */
		_Float32 mul_rev;	/* value to multiply to revert back */
	} f32;
#endif
#if defined(HAVE_FLOAT64)
	struct {
		_Float64 r_init;	/* initialization value for r */
		_Float64 r[2];		/* result of computation */
		_Float64 add;		/* value to add */
		_Float64 add_rev;	/* value to add to revert back */
		_Float64 mul;		/* value to multiply */
		_Float64 mul_rev;	/* value to multiply to revert back */
	} f64;
#endif
#if defined(HAVE_FLOAT80)
	struct {
		__float80 r_init;	/* initialization value for r */
		__float80 r[2];		/* result of computation */
		__float80 add;		/* value to add */
		__float80 add_rev;	/* value to add to revert back */
		__float80 mul;		/* value to multiply */
		__float80 mul_rev;	/* value to multiply to revert back */
	} f80;
#endif
#if defined(HAVE_FLOAT128)
	struct {
		__float128 r_init;	/* initialization value for r */
		__float128 r[2];	/* result of computation */
		__float128 add;		/* value to add */
		__float128 add_rev;	/* value to add to revert back */
		__float128 mul;		/* value to multiply */
		__float128 mul_rev;	/* value to multiply to revert back */
	} f128;
#endif
} fp_data_t;

typedef double (*stress_fp_func_t)(
	const stress_args_t *args,
	fp_data_t *fp_data,
	const int index);

static double stress_fp_all(
	const stress_args_t *args,
	fp_data_t *fp_data,
	const int index);

#define STRESS_FP_ADD(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	const stress_args_t *args,					\
	fp_data_t *fp_data,						\
	const int index)						\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[index] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		fp_data[0].field.r[index] += fp_data[0].field.add;	\
		fp_data[0].field.r[index] += fp_data[0].field.add_rev;	\
		fp_data[1].field.r[index] += fp_data[1].field.add;	\
		fp_data[1].field.r[index] += fp_data[1].field.add_rev;	\
		fp_data[2].field.r[index] += fp_data[2].field.add;	\
		fp_data[2].field.r[index] += fp_data[2].field.add_rev;	\
		fp_data[3].field.r[index] += fp_data[3].field.add;	\
		fp_data[3].field.r[index] += fp_data[3].field.add_rev;	\
		fp_data[4].field.r[index] += fp_data[4].field.add;	\
		fp_data[4].field.r[index] += fp_data[4].field.add_rev;	\
		fp_data[5].field.r[index] += fp_data[5].field.add;	\
		fp_data[5].field.r[index] += fp_data[5].field.add_rev;	\
		fp_data[6].field.r[index] += fp_data[6].field.add;	\
		fp_data[6].field.r[index] += fp_data[6].field.add_rev;	\
		fp_data[7].field.r[index] += fp_data[7].field.add;	\
		fp_data[7].field.r[index] += fp_data[7].field.add_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_FP_MUL(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	const stress_args_t *args,					\
	fp_data_t *fp_data,						\
	const int index)						\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[index] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; i < loops ; i++) {					\
		fp_data[0].field.r[index] *= fp_data[0].field.mul;	\
		fp_data[0].field.r[index] *= fp_data[0].field.mul_rev;	\
		fp_data[1].field.r[index] *= fp_data[1].field.mul;	\
		fp_data[1].field.r[index] *= fp_data[1].field.mul_rev;	\
		fp_data[2].field.r[index] *= fp_data[2].field.mul;	\
		fp_data[2].field.r[index] *= fp_data[2].field.mul_rev;	\
		fp_data[3].field.r[index] *= fp_data[3].field.mul;	\
		fp_data[3].field.r[index] *= fp_data[3].field.mul_rev;	\
		fp_data[4].field.r[index] *= fp_data[4].field.mul;	\
		fp_data[4].field.r[index] *= fp_data[4].field.mul_rev;	\
		fp_data[5].field.r[index] *= fp_data[5].field.mul;	\
		fp_data[5].field.r[index] *= fp_data[5].field.mul_rev;	\
		fp_data[6].field.r[index] *= fp_data[6].field.mul;	\
		fp_data[6].field.r[index] *= fp_data[6].field.mul_rev;	\
		fp_data[7].field.r[index] *= fp_data[7].field.mul;	\
		fp_data[7].field.r[index] *= fp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

#define STRESS_FP_DIV(field, name, do_bogo_ops)				\
static double TARGET_CLONES OPTIMIZE3 name(				\
	const stress_args_t *args,					\
	fp_data_t *fp_data,						\
	const int index)						\
{									\
	register int i;							\
	const int loops = LOOPS_PER_CALL >> 1;				\
	double t1, t2;							\
									\
	for (i = 0; i < FP_ELEMENTS; i++) {				\
		fp_data[i].field.r[index] = fp_data[i].field.r_init;	\
	}								\
									\
	t1 = stress_time_now();						\
	for (i = 0; stress_continue_flag() && (i < loops); i++) {	\
		fp_data[0].field.r[index] /= fp_data[0].field.mul;	\
		fp_data[0].field.r[index] /= fp_data[0].field.mul_rev;	\
		fp_data[1].field.r[index] /= fp_data[1].field.mul;	\
		fp_data[1].field.r[index] /= fp_data[1].field.mul_rev;	\
		fp_data[2].field.r[index] /= fp_data[2].field.mul;	\
		fp_data[2].field.r[index] /= fp_data[2].field.mul_rev;	\
		fp_data[3].field.r[index] /= fp_data[3].field.mul;	\
		fp_data[3].field.r[index] /= fp_data[3].field.mul_rev;	\
		fp_data[4].field.r[index] /= fp_data[4].field.mul;	\
		fp_data[4].field.r[index] /= fp_data[4].field.mul_rev;	\
		fp_data[5].field.r[index] /= fp_data[5].field.mul;	\
		fp_data[5].field.r[index] /= fp_data[5].field.mul_rev;	\
		fp_data[6].field.r[index] /= fp_data[6].field.mul;	\
		fp_data[6].field.r[index] /= fp_data[6].field.mul_rev;	\
		fp_data[7].field.r[index] /= fp_data[7].field.mul;	\
		fp_data[7].field.r[index] /= fp_data[7].field.mul_rev;	\
	}								\
	t2 = stress_time_now();						\
									\
	if (do_bogo_ops)						\
		stress_bogo_inc(args);					\
	return t2 - t1;							\
}

STRESS_FP_ADD(ld, stress_fp_ldouble_add, true)
STRESS_FP_MUL(ld, stress_fp_ldouble_mul, true)
STRESS_FP_DIV(ld, stress_fp_ldouble_div, true)

STRESS_FP_ADD(d, stress_fp_double_add, true)
STRESS_FP_MUL(d, stress_fp_double_mul, true)
STRESS_FP_DIV(d, stress_fp_double_div, true)

STRESS_FP_ADD(f, stress_fp_float_add, true)
STRESS_FP_MUL(f, stress_fp_float_mul, true)
STRESS_FP_DIV(f, stress_fp_float_div, true)

#if defined(HAVE_FLOAT32)
STRESS_FP_ADD(f32, stress_fp_float32_add, false)
STRESS_FP_MUL(f32, stress_fp_float32_mul, false)
STRESS_FP_DIV(f32, stress_fp_float32_div, false)
#endif

#if defined(HAVE_FLOAT64)
STRESS_FP_ADD(f64, stress_fp_float64_add, false)
STRESS_FP_MUL(f64, stress_fp_float64_mul, false)
STRESS_FP_DIV(f64, stress_fp_float64_div, false)
#endif

#if defined(HAVE_FLOAT80)
STRESS_FP_ADD(f80, stress_fp_float80_add, false)
STRESS_FP_MUL(f80, stress_fp_float80_mul, false)
STRESS_FP_DIV(f80, stress_fp_float80_div, false)
#endif

#if defined(HAVE_FLOAT128)
STRESS_FP_ADD(f128, stress_fp_float128_add, false)
STRESS_FP_MUL(f128, stress_fp_float128_mul, false)
STRESS_FP_DIV(f128, stress_fp_float128_div, false)
#endif

typedef struct {
	const char *name;
	const char *description;
	const stress_fp_func_t	fp_func;
	const int fp_type;
	double duration;
	double ops;
} stress_fp_funcs_t;

static stress_fp_funcs_t stress_fp_funcs[] = {
	{ "all",		"all fp methods",	stress_fp_all,		STRESS_FP_TYPE_ALL,		0.0, 0.0 },

#if defined(HAVE_FLOAT128)
	{ "float128add",	"float128 add",		stress_fp_float128_add,	STRESS_FP_TYPE_FLOAT128,	0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT80)
	{ "float80add",		"float80 add",		stress_fp_float80_add,	STRESS_FP_TYPE_FLOAT80,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT64)
	{ "float64add",		"float64 add",		stress_fp_float64_add,	STRESS_FP_TYPE_FLOAT64,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT32)
	{ "float32add",		"float32 add",		stress_fp_float32_add,	STRESS_FP_TYPE_FLOAT32,		0.0, 0.0 },
#endif
	{ "floatadd",		"float add",		stress_fp_float_add,	STRESS_FP_TYPE_FLOAT,		0.0, 0.0 },
	{ "doubleadd",		"double add",		stress_fp_double_add,	STRESS_FP_TYPE_DOUBLE,		0.0, 0.0 },
	{ "ldoubleadd",		"long double add",	stress_fp_ldouble_add,	STRESS_FP_TYPE_LONG_DOUBLE,	0.0, 0.0 },

#if defined(HAVE_FLOAT128)
	{ "float128mul",	"float128 multiply",	stress_fp_float128_mul,	STRESS_FP_TYPE_FLOAT128,	0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT80)
	{ "float80mul",		"float80 multiply",	stress_fp_float80_mul,	STRESS_FP_TYPE_FLOAT80,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT64)
	{ "float64mul",		"float64 multiply",	stress_fp_float64_mul,	STRESS_FP_TYPE_FLOAT64,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT32)
	{ "float32mul",		"float32 multiply",	stress_fp_float32_mul,	STRESS_FP_TYPE_FLOAT32,		0.0, 0.0 },
#endif
	{ "floatmul",		"float multiply",	stress_fp_float_mul,	STRESS_FP_TYPE_FLOAT,		0.0, 0.0 },
	{ "doublemul",		"double multiply",	stress_fp_double_mul,	STRESS_FP_TYPE_DOUBLE,		0.0, 0.0 },
	{ "ldoublemul",		"long double multiply",	stress_fp_ldouble_mul,	STRESS_FP_TYPE_LONG_DOUBLE,	0.0, 0.0 },

#if defined(HAVE_FLOAT128)
	{ "float128div",	"float128 divide",	stress_fp_float128_div,	STRESS_FP_TYPE_FLOAT128,	0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT80)
	{ "float80div",		"float80 divide",	stress_fp_float80_div,	STRESS_FP_TYPE_FLOAT80,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT64)
	{ "float64div",		"float64 divide",	stress_fp_float64_div,	STRESS_FP_TYPE_FLOAT64,		0.0, 0.0 },
#endif
#if defined(HAVE_FLOAT32)
	{ "float32div",		"float32 divide",	stress_fp_float32_div,	STRESS_FP_TYPE_FLOAT32,		0.0, 0.0 },
#endif
	{ "floatdiv",		"float divide",		stress_fp_float_div,	STRESS_FP_TYPE_FLOAT,		0.0, 0.0 },
	{ "doublediv",		"double divide",	stress_fp_double_div,	STRESS_FP_TYPE_DOUBLE,		0.0, 0.0 },
	{ "ldoublediv",		"long double divide",	stress_fp_ldouble_div,	STRESS_FP_TYPE_LONG_DOUBLE,	0.0, 0.0 },
};

typedef struct {
	const int fp_type;
	const char *fp_description;
} fp_type_map_t;

static const fp_type_map_t fp_type_map[] = {
	{ STRESS_FP_TYPE_LONG_DOUBLE,	"long double" },
	{ STRESS_FP_TYPE_DOUBLE,	"double" },
	{ STRESS_FP_TYPE_FLOAT,		"float" },
	{ STRESS_FP_TYPE_FLOAT32,	"float32" },
	{ STRESS_FP_TYPE_FLOAT64,	"float64" },
	{ STRESS_FP_TYPE_FLOAT80,	"float80" },
	{ STRESS_FP_TYPE_FLOAT128,	"float128" },
	{ STRESS_FP_TYPE_ALL,		"all" },
};

static const char *stress_fp_type(const int fp_type)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(fp_type_map); i++) {
		if (fp_type == fp_type_map[i].fp_type)
			return fp_type_map[i].fp_description;
	}
	return "unknown";
}

static void stress_fp_call_method(
	const stress_args_t *args,
	fp_data_t *fp_data,
	const size_t method,
	const bool verify)
{
	double dt;
	stress_fp_funcs_t *func = &stress_fp_funcs[method];

	dt = func->fp_func(args, fp_data, 0);
	func->duration += dt;
	func->ops += (FP_ELEMENTS * LOOPS_PER_CALL);

	if ((method > 0) && (method < SIZEOF_ARRAY(stress_fp_funcs)) && verify) {
		register size_t i;
		const int fp_type = stress_fp_funcs[method].fp_type;
		const char *method_name = stress_fp_funcs[method].name;
		const char *fp_description = stress_fp_type(fp_type);

		dt = func->fp_func(args, fp_data, 1);
		func->duration += dt;
		func->ops += (FP_ELEMENTS * LOOPS_PER_CALL);

		/*
		 *  a SIGALRM during 2nd computation pre-verification can
		 *  cause long doubles on some arches to abort early, so
		 *  don't verify these results
		 */
		if (!stress_continue_flag())
			return;

		for (i = 0; i < FP_ELEMENTS; i++) {
			long double r0, r1;
			int ret;

			switch (fp_type) {
			case STRESS_FP_TYPE_LONG_DOUBLE:
				ret = memcmp(&fp_data[i].ld.r[0], &fp_data[i].ld.r[1], sizeof(fp_data[i].ld.r[0]));
				r0 = (long double)fp_data[i].ld.r[0];
				r1 = (long double)fp_data[i].ld.r[1];
				break;
			case STRESS_FP_TYPE_DOUBLE:
				ret = memcmp(&fp_data[i].d.r[0], &fp_data[i].d.r[1], sizeof(fp_data[i].d.r[0]));
				r0 = (long double)fp_data[i].d.r[0];
				r1 = (long double)fp_data[i].d.r[1];
				break;
			case STRESS_FP_TYPE_FLOAT:
				ret = memcmp(&fp_data[i].f.r[0], &fp_data[i].f.r[1], sizeof(fp_data[i].f.r[0]));
				r0 = (long double)fp_data[i].f.r[0];
				r1 = (long double)fp_data[i].f.r[1];
				break;
#if defined(HAVE_FLOAT32)
			case STRESS_FP_TYPE_FLOAT32:
				ret = memcmp(&fp_data[i].f32.r[0], &fp_data[i].f32.r[1], sizeof(fp_data[i].f32.r[0]));
				r0 = (long double)fp_data[i].f32.r[0];
				r1 = (long double)fp_data[i].f32.r[1];
				break;
#endif
#if defined(HAVE_FLOAT64)
			case STRESS_FP_TYPE_FLOAT64:
				ret = memcmp(&fp_data[i].f64.r[0], &fp_data[i].f64.r[1], sizeof(fp_data[i].f64.r[0]));
				r0 = (long double)fp_data[i].f64.r[0];
				r1 = (long double)fp_data[i].f64.r[1];
				break;
#endif
#if defined(HAVE_FLOAT80)
			case STRESS_FP_TYPE_FLOAT80:
				ret = memcmp(&fp_data[i].f80.r[0], &fp_data[i].f80.r[1], sizeof(fp_data[i].f80.r[0]));
				r0 = (long double)fp_data[i].f80.r[0];
				r1 = (long double)fp_data[i].f80.r[1];
				break;
#endif
#if defined(HAVE_FLOAT128)
			case STRESS_FP_TYPE_FLOAT128:
				ret = memcmp(&fp_data[i].f128.r[0], &fp_data[i].f128.r[1], sizeof(fp_data[i].f128.r[0]));
				r0 = (long double)fp_data[i].f128.r[0];
				r1 = (long double)fp_data[i].f128.r[1];
				break;
#endif
			default:
				/* Should never happen! */
				return;
			}
			if (ret) {
				pr_fail("%s %s %s verification failure on element %zd, got %Lf, expected %Lf\n",
					args->name, fp_description, method_name, i, r0, r1);
			}
		}
	}
}

static double stress_fp_all(
	const stress_args_t *args,
	fp_data_t *fp_data,
	const int index)
{
	size_t i;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);
	(void)index;

	for (i = 1; i < SIZEOF_ARRAY(stress_fp_funcs); i++) {
		stress_fp_call_method(args, fp_data, i, verify);
	}
	return 0.0;
}

/*
 *  stress_set_fp_method()
 *	set the default vector floating point stress method
 */
static int stress_set_fp_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_fp_funcs); i++) {
		if (!strcmp(stress_fp_funcs[i].name, name)) {
			stress_set_setting("fp-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "fp-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_fp_funcs); i++) {
		(void)fprintf(stderr, " %s", stress_fp_funcs[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static int stress_fp(const stress_args_t *args)
{
	size_t i, mmap_size;
	fp_data_t *fp_data;
	size_t fp_method = 0;	/* "all" */
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	mmap_size = FP_ELEMENTS * sizeof(*fp_data);
	fp_data = (fp_data_t *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (fp_data == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate %d floating point elements, skipping stressor\n",
			args->name, FP_ELEMENTS);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("fp-method", &fp_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

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

#if defined(HAVE_FLOAT32)
		fp_data[i].f32.r_init = (_Float32)ld;
		fp_data[i].f32.r[0] = (_Float32)ld;
		fp_data[i].f32.r[1] = (_Float32)ld;
#endif

#if defined(HAVE_FLOAT64)
		fp_data[i].f64.r_init = (_Float64)ld;
		fp_data[i].f64.r[0] = (_Float64)ld;
		fp_data[i].f64.r[1] = (_Float64)ld;
#endif

#if defined(HAVE_FLOAT80)
		fp_data[i].f80.r_init = (__float80)ld;
		fp_data[i].f80.r[0] = (__float80)ld;
		fp_data[i].f80.r[1] = (__float80)ld;
#endif

#if defined(HAVE_FLOAT128)
		fp_data[i].f128.r_init = (__float128)ld;
		fp_data[i].f128.r[0] = (__float128)ld;
		fp_data[i].f128.r[1] = (__float128)ld;
#endif

		r = stress_mwc32();
		ld = (long double)r / ((long double)(1ULL << 31));
		fp_data[i].ld.add = ld;
		fp_data[i].d.add = (double)ld;
		fp_data[i].f.add = (float)ld;
#if defined(HAVE_FLOAT32)
		fp_data[i].f32.add = (_Float32)ld;
#endif
#if defined(HAVE_FLOAT64)
		fp_data[i].f64.add = (_Float64)ld;
#endif
#if defined(HAVE_FLOAT80)
		fp_data[i].f80.add = (__float80)ld;
#endif
#if defined(HAVE_FLOAT128)
		fp_data[i].f128.add = (__float128)ld;
#endif

		ld = -(ld * 0.992);
		fp_data[i].ld.add_rev = ld;
		fp_data[i].d.add_rev = (double)ld;
		fp_data[i].f.add_rev = (float)ld;
#if defined(HAVE_FLOAT32)
		fp_data[i].f32.add_rev = (_Float32)ld;
#endif
#if defined(HAVE_FLOAT64)
		fp_data[i].f64.add_rev = (_Float64)ld;
#endif
#if defined(HAVE_FLOAT80)
		fp_data[i].f80.add_rev = (__float80)ld;
#endif
#if defined(HAVE_FLOAT128)
		fp_data[i].f128.add_rev = (__float128)ld;
#endif

		r = stress_mwc32();
		ld = (long double)i + (long double)r / ((long double)(1ULL << 36));
		fp_data[i].ld.mul = ld;
		fp_data[i].d.mul = (double)ld;
		fp_data[i].f.mul = (float)ld;
#if defined(HAVE_FLOAT32)
		fp_data[i].f32.mul = (_Float32)ld;
#endif
#if defined(HAVE_FLOAT64)
		fp_data[i].f64.mul = (_Float64)ld;
#endif
#if defined(HAVE_FLOAT80)
		fp_data[i].f80.mul = (__float80)ld;
#endif
#if defined(HAVE_FLOAT128)
		fp_data[i].f128.mul = (__float128)ld;
#endif

		ld = 0.9995 / ld;
		fp_data[i].ld.mul_rev = ld;
		fp_data[i].d.mul_rev = (double)ld;
		fp_data[i].f.mul_rev = (float)ld;
#if defined(HAVE_FLOAT32)
		fp_data[i].f32.mul_rev = (_Float32)ld;
#endif
#if defined(HAVE_FLOAT64)
		fp_data[i].f64.mul_rev = (_Float64)ld;
#endif
#if defined(HAVE_FLOAT80)
		fp_data[i].f80.mul_rev = (__float80)ld;
#endif
#if defined(HAVE_FLOAT128)
		fp_data[i].f128.mul_rev = (__float128)ld;
#endif
	}

	do {
		stress_fp_call_method(args, fp_data, fp_method, verify);
	} while (stress_continue(args));

	for (i = 1; i < SIZEOF_ARRAY(stress_fp_funcs); i++) {
		const double ops = stress_fp_funcs[i].ops;
		const double duration = stress_fp_funcs[i].duration;
		if ((duration > 0.0) && (ops > 0.0)) {
			char msg[64];
			const double rate = stress_fp_funcs[i].ops / stress_fp_funcs[i].duration;

			(void)snprintf(msg, sizeof(msg), "Mfp-ops per sec, %-20s", stress_fp_funcs[i].description);
			stress_metrics_set(args, i - 1, msg, rate / 1000000.0);
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)fp_data, mmap_size);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_fp_method,	stress_set_fp_method },
};

stressor_info_t stress_fp_info = {
	.stressor = stress_fp,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
