// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2021-2023 Colin Ian King
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-put.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

#define LOOPS_PER_CALL	(65536)

static const stress_help_t help[] = {
	{ NULL,	"vecfp N",	 "start N workers performing vector math ops" },
	{ NULL,	"vecfp-ops N",	"stop after N vector math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_VECMATH)

typedef struct {
	struct {
		double r_init;	/* initialization value for r */
		double *r;	/* pointer to result of computation r1 or r2 */
		double r1;	/* result of computation */
		double r2;	/* result of computation for checking */
		double add;	/* value to add */
		double add_rev;	/* value to add to revert back */
		double mul;	/* value to multiply */
		double mul_rev;	/* value to multiply to revert back */
	} d;
	struct {
		float r_init;	/* initialization value for r */
		float *r;	/* pointer to result of computation r1 or r2 */
		float r1;	/* result of computation */
		float r2;	/* result of computation for checking */
		float add;	/* value to add */
		float add_rev;	/* value to add to revert back */
		float mul;	/* value to multiply */
		float mul_rev;	/* value to multiply to revert back */
	} f;
} stress_vecfp_init;

typedef double (*stress_vecfp_func_t)(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init,
	bool *success);

/*
 *  float vectors, named by vfloatwN where N = number of elements width
 */
#define VEC_TYPE_T(type, elements)						\
typedef union {									\
	type v	 ALIGNED(2048) __attribute__ ((vector_size(sizeof(type) * elements)));\
	type f[elements] ALIGNED(2048);						\
} stress_vecfp_ ## type ## _ ## elements ## _t;

VEC_TYPE_T(float, 256)
VEC_TYPE_T(float, 128)
VEC_TYPE_T(float, 64)
VEC_TYPE_T(float, 32)
VEC_TYPE_T(float, 16)
VEC_TYPE_T(float, 8)

VEC_TYPE_T(double, 256)
VEC_TYPE_T(double, 128)
VEC_TYPE_T(double, 64)
VEC_TYPE_T(double, 32)
VEC_TYPE_T(double, 16)
VEC_TYPE_T(double, 8)

static double stress_vecfp_all(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init,
	bool *success);

#define STRESS_VEC_ADD(field, name, type)			\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init,				\
	bool *success)						\
{								\
	type r ALIGN64, add ALIGN64, add_rev ALIGN64;		\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	(void)success;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].field.r_init;		\
		add.f[i] = vecfp_init[i].field.add;		\
		add_rev.f[i] = vecfp_init[i].field.add_rev;	\
	}							\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops ; i++) {				\
		r.v = r.v + add.v;				\
		r.v = r.v + add_rev.v;				\
	}							\
	t2 = stress_time_now();					\
								\
	for (i = 0; i < n; i++) {				\
		*vecfp_init[i].field.r = r.f[i];		\
	}							\
	stress_bogo_inc(args);					\
	return t2 - t1;						\
}

#define STRESS_VEC_MUL(field, name, type)			\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init,				\
	bool *success)						\
{								\
	type r ALIGN64, mul ALIGN64, mul_rev ALIGN64;		\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	(void)success;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].field.r_init;		\
		mul.f[i] = vecfp_init[i].field.mul;		\
		mul_rev.f[i] = vecfp_init[i].field.mul_rev;	\
	}							\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops ; i++) {				\
		r.v = r.v * mul.v;				\
		r.v = r.v * mul_rev.v;				\
	}							\
	t2 = stress_time_now();					\
								\
	for (i = 0; i < n; i++) {				\
		*vecfp_init[i].field.r = r.f[i];		\
	}							\
	stress_bogo_inc(args);					\
	return t2 - t1;						\
}

#define STRESS_VEC_DIV(field, name, type)			\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init,				\
	bool *success)						\
{								\
	type r ALIGN64, mul ALIGN64, mul_rev ALIGN64;		\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	(void)success;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].field.r_init;		\
		mul.f[i] = vecfp_init[i].field.mul;		\
		mul_rev.f[i] = vecfp_init[i].field.mul_rev;	\
	}							\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops ; i++) {				\
		r.v = r.v / mul.v;				\
		r.v = r.v / mul_rev.v;				\
	}							\
	t2 = stress_time_now();					\
	for (i = 0; i < n; i++) {				\
		*vecfp_init[i].field.r = r.f[i];		\
	}							\
								\
	stress_bogo_inc(args);					\
	return t2 - t1;						\
}

#define STRESS_VEC_NEG(field, name, type)			\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init,				\
	bool *success)						\
{								\
	type r;							\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	(void)success;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].field.r_init;		\
	}							\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops ; i++) {				\
		r.v = -r.v;					\
	}							\
	t2 = stress_time_now();					\
	for (i = 0; i < n; i++) {				\
		*vecfp_init[i].field.r = r.f[i];		\
	}							\
								\
	stress_bogo_inc(args);					\
	return t2 - t1;						\
}

STRESS_VEC_ADD(f, stress_vecfp_float_add_128, stress_vecfp_float_64_t)
STRESS_VEC_ADD(f, stress_vecfp_float_add_64, stress_vecfp_float_64_t)
STRESS_VEC_ADD(f, stress_vecfp_float_add_32, stress_vecfp_float_32_t)
STRESS_VEC_ADD(f, stress_vecfp_float_add_16, stress_vecfp_float_16_t)
STRESS_VEC_ADD(f, stress_vecfp_float_add_8, stress_vecfp_float_8_t)

STRESS_VEC_MUL(f, stress_vecfp_float_mul_128, stress_vecfp_float_64_t)
STRESS_VEC_MUL(f, stress_vecfp_float_mul_64, stress_vecfp_float_64_t)
STRESS_VEC_MUL(f, stress_vecfp_float_mul_32, stress_vecfp_float_32_t)
STRESS_VEC_MUL(f, stress_vecfp_float_mul_16, stress_vecfp_float_16_t)
STRESS_VEC_MUL(f, stress_vecfp_float_mul_8, stress_vecfp_float_8_t)

STRESS_VEC_DIV(f, stress_vecfp_float_div_128, stress_vecfp_float_64_t)
STRESS_VEC_DIV(f, stress_vecfp_float_div_64, stress_vecfp_float_64_t)
STRESS_VEC_DIV(f, stress_vecfp_float_div_32, stress_vecfp_float_32_t)
STRESS_VEC_DIV(f, stress_vecfp_float_div_16, stress_vecfp_float_16_t)
STRESS_VEC_DIV(f, stress_vecfp_float_div_8, stress_vecfp_float_8_t)

STRESS_VEC_NEG(f, stress_vecfp_float_neg_128, stress_vecfp_float_64_t)
STRESS_VEC_NEG(f, stress_vecfp_float_neg_64, stress_vecfp_float_64_t)
STRESS_VEC_NEG(f, stress_vecfp_float_neg_32, stress_vecfp_float_32_t)
STRESS_VEC_NEG(f, stress_vecfp_float_neg_16, stress_vecfp_float_16_t)
STRESS_VEC_NEG(f, stress_vecfp_float_neg_8, stress_vecfp_float_8_t)

STRESS_VEC_ADD(d, stress_vecfp_double_add_128, stress_vecfp_double_128_t)
STRESS_VEC_ADD(d, stress_vecfp_double_add_64, stress_vecfp_double_64_t)
STRESS_VEC_ADD(d, stress_vecfp_double_add_32, stress_vecfp_double_32_t)
STRESS_VEC_ADD(d, stress_vecfp_double_add_16, stress_vecfp_double_16_t)
STRESS_VEC_ADD(d, stress_vecfp_double_add_8, stress_vecfp_double_8_t)

STRESS_VEC_MUL(d, stress_vecfp_double_mul_128, stress_vecfp_double_128_t)
STRESS_VEC_MUL(d, stress_vecfp_double_mul_64, stress_vecfp_double_64_t)
STRESS_VEC_MUL(d, stress_vecfp_double_mul_32, stress_vecfp_double_32_t)
STRESS_VEC_MUL(d, stress_vecfp_double_mul_16, stress_vecfp_double_16_t)
STRESS_VEC_MUL(d, stress_vecfp_double_mul_8, stress_vecfp_double_8_t)

STRESS_VEC_DIV(d, stress_vecfp_double_div_128, stress_vecfp_double_128_t)
STRESS_VEC_DIV(d, stress_vecfp_double_div_64, stress_vecfp_double_64_t)
STRESS_VEC_DIV(d, stress_vecfp_double_div_32, stress_vecfp_double_32_t)
STRESS_VEC_DIV(d, stress_vecfp_double_div_16, stress_vecfp_double_16_t)
STRESS_VEC_DIV(d, stress_vecfp_double_div_8, stress_vecfp_double_8_t)

STRESS_VEC_NEG(d, stress_vecfp_double_neg_128, stress_vecfp_double_128_t)
STRESS_VEC_NEG(d, stress_vecfp_double_neg_64, stress_vecfp_double_64_t)
STRESS_VEC_NEG(d, stress_vecfp_double_neg_32, stress_vecfp_double_32_t)
STRESS_VEC_NEG(d, stress_vecfp_double_neg_16, stress_vecfp_double_16_t)
STRESS_VEC_NEG(d, stress_vecfp_double_neg_8, stress_vecfp_double_8_t)

typedef struct {
	const char *name;
	const stress_vecfp_func_t vecfp_func;
	const size_t elements;
	double duration;
	double ops;
} stress_vecfp_funcs_t;

static stress_vecfp_funcs_t stress_vecfp_funcs[] = {
	{ "all",		stress_vecfp_all, 0, 0.0, 0.0 },

	{ "floatv128add",	stress_vecfp_float_add_128, 128, 0.0, 0.0 },
	{ "floatv64add",	stress_vecfp_float_add_64, 64, 0.0, 0.0 },
	{ "floatv32add",	stress_vecfp_float_add_32, 32, 0.0, 0.0 },
	{ "floatv16add",	stress_vecfp_float_add_16, 16, 0.0, 0.0 },
	{ "floatv8add",		stress_vecfp_float_add_8, 8, 0.0, 0.0 },

	{ "floatv128mul",	stress_vecfp_float_mul_128, 128, 0.0, 0.0 },
	{ "floatv64mul",	stress_vecfp_float_mul_64, 64, 0.0, 0.0 },
	{ "floatv32mul",	stress_vecfp_float_mul_32, 32, 0.0, 0.0 },
	{ "floatv16mul",	stress_vecfp_float_mul_16, 16, 0.0, 0.0 },
	{ "floatv8mul",		stress_vecfp_float_mul_8, 8, 0.0, 0.0 },

	{ "floatv128div",	stress_vecfp_float_div_128, 128, 0.0, 0.0 },
	{ "floatv64div",	stress_vecfp_float_div_64, 64, 0.0, 0.0 },
	{ "floatv32div",	stress_vecfp_float_div_32, 32, 0.0, 0.0 },
	{ "floatv16div",	stress_vecfp_float_div_16, 16, 0.0, 0.0 },
	{ "floatv8div",		stress_vecfp_float_div_8, 8, 0.0, 0.0 },

	{ "floatv128neg",	stress_vecfp_float_neg_128, 128, 0.0, 0.0 },
	{ "floatv64neg",	stress_vecfp_float_neg_64, 64, 0.0, 0.0 },
	{ "floatv32neg",	stress_vecfp_float_neg_32, 32, 0.0, 0.0 },
	{ "floatv16neg",	stress_vecfp_float_neg_16, 16, 0.0, 0.0 },
	{ "floatv8neg",		stress_vecfp_float_neg_8, 8, 0.0, 0.0 },

	{ "doublev128add",	stress_vecfp_double_add_128, 128, 0.0, 0.0 },
	{ "doublev64add",	stress_vecfp_double_add_64, 64, 0.0, 0.0 },
	{ "doublev32add",	stress_vecfp_double_add_32, 32, 0.0, 0.0 },
	{ "doublev16add",	stress_vecfp_double_add_16, 16, 0.0, 0.0 },
	{ "doublev8add",	stress_vecfp_double_add_8, 8, 0.0, 0.0 },

	{ "doublev128mul",	stress_vecfp_double_mul_128, 128, 0.0, 0.0 },
	{ "doublev64mul",	stress_vecfp_double_mul_64, 64, 0.0, 0.0 },
	{ "doublev32mul",	stress_vecfp_double_mul_32, 32, 0.0, 0.0 },
	{ "doublev16mul",	stress_vecfp_double_mul_16, 16, 0.0, 0.0 },
	{ "doublev8mul",	stress_vecfp_double_mul_8, 8, 0.0, 0.0 },

	{ "doublev128div",	stress_vecfp_double_div_128, 128, 0.0, 0.0 },
	{ "doublev64div",	stress_vecfp_double_div_64, 64, 0.0, 0.0 },
	{ "doublev32div",	stress_vecfp_double_div_32, 32, 0.0, 0.0 },
	{ "doublev16div",	stress_vecfp_double_div_16, 16, 0.0, 0.0 },
	{ "doublev8div",	stress_vecfp_double_div_8, 8, 0.0, 0.0 },

	{ "doublev128neg",	stress_vecfp_double_neg_128, 128, 0.0, 0.0 },
	{ "doublev64neg",	stress_vecfp_double_neg_64, 64, 0.0, 0.0 },
	{ "doublev32neg",	stress_vecfp_double_neg_32, 32, 0.0, 0.0 },
	{ "doublev16neg",	stress_vecfp_double_neg_16, 16, 0.0, 0.0 },
	{ "doublev8neg",	stress_vecfp_double_neg_8, 8, 0.0, 0.0 },
};

static void OPTIMIZE3 stress_vecfp_call_method(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init,
	const size_t method,
	bool *success)
{
	stress_vecfp_funcs_t *const func = &stress_vecfp_funcs[method];
	const double ops = (double)(LOOPS_PER_CALL * func->elements);
	size_t i;
	const bool verify = !!(g_opt_flags & OPT_FLAGS_VERIFY);

	for (i = 0; i < func->elements; i++) {
		vecfp_init[i].d.r = &vecfp_init[i].d.r1;
		vecfp_init[i].f.r = &vecfp_init[i].f.r1;
	}
	func->duration += func->vecfp_func(args, vecfp_init, success);
	func->ops += ops;

	if (verify) {
		for (i = 0; i < func->elements; i++) {
			vecfp_init[i].d.r = &vecfp_init[i].d.r2;
			vecfp_init[i].f.r = &vecfp_init[i].f.r2;
		}
		func->duration += func->vecfp_func(args, vecfp_init, success);
		func->ops += ops;

		for (i = 0; i < func->elements; i++) {
			if (fabs(vecfp_init[i].d.r1 - vecfp_init[i].d.r2) > (double)0.0001) {
				pr_fail("%s: %s double vector operation result mismatch, got %f, expected %f\n",
					args->name, stress_vecfp_funcs[method].name,
					vecfp_init[i].d.r2, vecfp_init[i].d.r1);
				*success = false;
			}
			if (fabsf(vecfp_init[i].f.r1 - vecfp_init[i].f.r2) > (float)0.0001) {
				pr_fail("%s: %s float vector operation result mismatch, got %f, expected %f\n",
					args->name, stress_vecfp_funcs[method].name,
					vecfp_init[i].f.r2, vecfp_init[i].f.r1);
				*success = false;
			}
		}
	}
}

static double stress_vecfp_all(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init,
	bool *success)
{
	size_t i;

	for (i = 1; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		stress_vecfp_call_method(args, vecfp_init, i, success);
	}
	return 0.0;
}

/*
 *  stress_set_vecfp_method()
 *	set the default vector floating point stress method
 */
static int stress_set_vecfp_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		if (!strcmp(stress_vecfp_funcs[i].name, name)) {
			stress_set_setting("vecfp-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "vecfp-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		(void)fprintf(stderr, " %s", stress_vecfp_funcs[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

static int stress_vecfp(const stress_args_t *args)
{
	size_t i, j, max_elements = 0, mmap_size;
	stress_vecfp_init *vecfp_init;
	size_t vecfp_method = 0;	/* "all" */
	bool success = true;

	for (i = 0; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		const size_t elements = stress_vecfp_funcs[i].elements;

		if (max_elements < elements)
			max_elements = elements;
	}

	mmap_size = max_elements * sizeof(*vecfp_init);
	vecfp_init = (stress_vecfp_init *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (vecfp_init == MAP_FAILED) {
		pr_inf_skip("%s: failed to allocate %zd initializing elements, skipping stressor\n",
			args->name, max_elements);
		return EXIT_NO_RESOURCE;
	}

	(void)stress_get_setting("vecfp-method", &vecfp_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < max_elements; i++) {
		double d;
		float f;
		uint32_t r;

		r = stress_mwc32();
		d = (double)i + (double)r / ((double)(1ULL << 38));
		vecfp_init[i].d.r_init = d;
		f = (float)i + (float)r / ((float)(1ULL << 38));
		vecfp_init[i].f.r_init = f;

		r = stress_mwc32();
		d = (double)r / ((double)(1ULL << 31));
		vecfp_init[i].d.add = d;
		vecfp_init[i].d.add_rev = -(d * 0.992);
		f = (float)r / ((float)(1ULL << 31));
		vecfp_init[i].f.add = f;
		vecfp_init[i].f.add_rev = -(f * (float)0.992);

		r = stress_mwc32();
		d = (double)i + (double)r / ((double)(1ULL << 36));
		vecfp_init[i].d.mul = d;
		vecfp_init[i].d.mul_rev = 0.9995 / d;
		f = (float)i + (float)r / ((float)(1ULL << 36));
		vecfp_init[i].f.mul = f;
		vecfp_init[i].f.mul_rev = (float)0.9995 / f;
	}

	do {
		stress_vecfp_call_method(args, vecfp_init, vecfp_method, &success);
	} while (stress_continue(args));

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		const double rate = (stress_vecfp_funcs[i].ops / stress_vecfp_funcs[i].duration) / 1000000.0;

		if (rate > 0.0) {
			char buffer[64];

			(void)snprintf(buffer, sizeof(buffer), "%s Mfp-ops/sec", stress_vecfp_funcs[i].name);
			stress_metrics_set(args, j, buffer, rate);
			j++;
		}
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)vecfp_init, mmap_size);

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_vecfp_method,	stress_set_vecfp_method },
};

stressor_info_t stress_vecfp_info = {
	.stressor = stress_vecfp,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
#else

/*
 *  stress_set_vecfp_method()
 *	set the default vector floating point stress method, no-op
 */
static int stress_set_vecfp_method(const char *name)
{
	(void)name;

	fprintf(stderr, "option --vecfp-method is not implemented, ignoring option '%s'\n", name);
	return 0;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_vecfp_method,	stress_set_vecfp_method },
};

stressor_info_t stress_vecfp_info = {
	.stressor = stress_unimplemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_OPTIONAL,
	.help = help,
	.unimplemented_reason = "built without compiler support for vector data/operations"
};
#endif
