/*
 * Copyright (C) 2021-2022 Colin Ian King
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
	double r_init;	/* initialization value for r */
	double r;	/* result of computation */
	double add;	/* value to add */
	double add_rev;	/* value to add to revert back */
	double mul;	/* value to multiply */
	double mul_rev;	/* value to multiply to revert back */
} stress_vecfp_init;

typedef double (*stress_vecfp_func_t)(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init);

/*
 *  float vectors, named by vfloatwN where N = number of elements width
 */
#define VEC_TYPE_T(type, elements)					\
typedef union {								\
	type v	 __attribute__ ((vector_size(sizeof(type) * elements)));\
	type f[elements];						\
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

#define STRESS_FLT_RND(type)					\
	1.0 + ((double)stress_mwc8() / 256000.0)

static double stress_vecfp_all(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init);

#define STRESS_VEC_ADD(name, type)				\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init)				\
{								\
	type r, add, add_rev;					\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].r_init;			\
		add.f[i] = vecfp_init[i].add;			\
		add_rev.f[i] = vecfp_init[i].add_rev;		\
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
		vecfp_init[i].r = r.f[i];			\
	}							\
	inc_counter(args);					\
	return t2 - t1;						\
}

#define STRESS_VEC_MUL(name, type)				\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init)				\
{								\
	type r, mul, mul_rev;					\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].r_init;			\
		mul.f[i] = vecfp_init[i].mul;			\
		mul_rev.f[i] = vecfp_init[i].mul_rev;		\
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
		vecfp_init[i].r = r.f[i];			\
	}							\
	inc_counter(args);					\
	return t2 - t1;						\
}

#define STRESS_VEC_DIV(name, type)				\
static double TARGET_CLONES OPTIMIZE3 name(			\
	const stress_args_t *args,				\
	stress_vecfp_init *vecfp_init)				\
{								\
	type r, mul, mul_rev;					\
	register int i;						\
	const int n = sizeof(r.f) / (sizeof(r.f[0]));		\
	const int loops = LOOPS_PER_CALL >> 1;			\
	double t1, t2;						\
								\
	for (i = 0; i < n; i++) {				\
		r.f[i] = vecfp_init[i].r_init;			\
		mul.f[i] = vecfp_init[i].mul;			\
		mul_rev.f[i] = vecfp_init[i].mul_rev;		\
	}							\
								\
	t1 = stress_time_now();					\
	for (i = 0; i < loops ; i++) {				\
		r.v = r.v / mul.v;				\
		r.v = r.v / mul_rev.v;				\
	}							\
	t2 = stress_time_now();					\
	for (i = 0; i < n; i++) {				\
		vecfp_init[i].r = r.f[i];			\
	}							\
								\
	inc_counter(args);					\
	return t2 - t1;						\
}

STRESS_VEC_ADD(stress_vecfp_float_add_128, stress_vecfp_float_64_t)
STRESS_VEC_ADD(stress_vecfp_float_add_64, stress_vecfp_float_64_t)
STRESS_VEC_ADD(stress_vecfp_float_add_32, stress_vecfp_float_32_t)
STRESS_VEC_ADD(stress_vecfp_float_add_16, stress_vecfp_float_16_t)
STRESS_VEC_ADD(stress_vecfp_float_add_8, stress_vecfp_float_8_t)

STRESS_VEC_MUL(stress_vecfp_float_mul_128, stress_vecfp_float_64_t)
STRESS_VEC_MUL(stress_vecfp_float_mul_64, stress_vecfp_float_64_t)
STRESS_VEC_MUL(stress_vecfp_float_mul_32, stress_vecfp_float_32_t)
STRESS_VEC_MUL(stress_vecfp_float_mul_16, stress_vecfp_float_16_t)
STRESS_VEC_MUL(stress_vecfp_float_mul_8, stress_vecfp_float_8_t)

STRESS_VEC_DIV(stress_vecfp_float_div_128, stress_vecfp_float_64_t)
STRESS_VEC_DIV(stress_vecfp_float_div_64, stress_vecfp_float_64_t)
STRESS_VEC_DIV(stress_vecfp_float_div_32, stress_vecfp_float_32_t)
STRESS_VEC_DIV(stress_vecfp_float_div_16, stress_vecfp_float_16_t)
STRESS_VEC_DIV(stress_vecfp_float_div_8, stress_vecfp_float_8_t)

STRESS_VEC_ADD(stress_vecfp_double_add_128, stress_vecfp_double_128_t)
STRESS_VEC_ADD(stress_vecfp_double_add_64, stress_vecfp_double_64_t)
STRESS_VEC_ADD(stress_vecfp_double_add_32, stress_vecfp_double_32_t)
STRESS_VEC_ADD(stress_vecfp_double_add_16, stress_vecfp_double_16_t)
STRESS_VEC_ADD(stress_vecfp_double_add_8, stress_vecfp_double_8_t)

STRESS_VEC_MUL(stress_vecfp_double_mul_128, stress_vecfp_double_128_t)
STRESS_VEC_MUL(stress_vecfp_double_mul_64, stress_vecfp_double_64_t)
STRESS_VEC_MUL(stress_vecfp_double_mul_32, stress_vecfp_double_32_t)
STRESS_VEC_MUL(stress_vecfp_double_mul_16, stress_vecfp_double_16_t)
STRESS_VEC_MUL(stress_vecfp_double_mul_8, stress_vecfp_double_8_t)

STRESS_VEC_DIV(stress_vecfp_double_div_128, stress_vecfp_double_128_t)
STRESS_VEC_DIV(stress_vecfp_double_div_64, stress_vecfp_double_64_t)
STRESS_VEC_DIV(stress_vecfp_double_div_32, stress_vecfp_double_32_t)
STRESS_VEC_DIV(stress_vecfp_double_div_16, stress_vecfp_double_16_t)
STRESS_VEC_DIV(stress_vecfp_double_div_8, stress_vecfp_double_8_t)

typedef struct {
	const char *name;
	stress_vecfp_func_t	vecfp_func;
	size_t elements;
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
};

static void stress_vecfp_call_method(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init,
	const size_t method)
{
	double dt, ops;
	stress_vecfp_funcs_t *func = &stress_vecfp_funcs[method];

	dt = func->vecfp_func(args, vecfp_init);
	func->duration += dt;
	ops = LOOPS_PER_CALL * func->elements;
	func->ops += ops;
}

static double stress_vecfp_all(
	const stress_args_t *args,
	stress_vecfp_init *vecfp_init)
{
	size_t i;

	for (i = 1; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		stress_vecfp_call_method(args, vecfp_init, i);
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
	size_t i, max_elements = 0, mmap_size;
	stress_vecfp_init *vecfp_init;
	size_t vecfp_method = 0;	/* "all" */

	for (i = 0; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
		const size_t elements = stress_vecfp_funcs[i].elements;
		if (max_elements < elements)
			max_elements = elements;
	}

	mmap_size = max_elements * sizeof(*vecfp_init);
	vecfp_init = (stress_vecfp_init *)mmap(NULL, mmap_size, PROT_READ | PROT_WRITE,
			MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (vecfp_init == MAP_FAILED) {
		pr_inf("%s: failed to allocate %zd initializing elements, skipping stressor\n",
			args->name, max_elements);
		return EXIT_NO_RESOURCE;
	}

	stress_get_setting("vecfp-method", &vecfp_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	for (i = 0; i < max_elements; i++) {
		double v;

		v = (double)i + (double)stress_mwc32() / ((double)(1ULL << 38));
		vecfp_init[i].r_init = v;

		v = (double)stress_mwc32() / ((double)(1ULL << 31));
		vecfp_init[i].add = v;
		vecfp_init[i].add_rev = -(v * 0.992);

		v = (double)i + (double)stress_mwc32() / ((double)(1ULL << 36));
		vecfp_init[i].mul = v;
		vecfp_init[i].mul_rev = 0.9995 / v;
	}

	do {
		stress_vecfp_call_method(args, vecfp_init, vecfp_method);
	} while (keep_stressing(args));

	if (args->instance == 0) {
		bool lock = false;

		pr_lock(&lock);
		pr_dbg_lock(&lock, "%s: compute throughput for just stressor instance 0:\n", args->name);
		pr_dbg_lock(&lock, "%s: %14.14s %13.13s\n",
			args->name, "Method", "Mfp-ops/sec");
		for (i = 1; i < SIZEOF_ARRAY(stress_vecfp_funcs); i++) {
			const double ops = stress_vecfp_funcs[i].ops;
			const double duration = stress_vecfp_funcs[i].duration;
			if (duration > 0.0 && ops > 0.0) {
				double rate = stress_vecfp_funcs[i].ops / stress_vecfp_funcs[i].duration;

				pr_dbg_lock(&lock, "%s: %14.14s %13.3f\n", args->name, stress_vecfp_funcs[i].name, rate / 1000000.0);
			}
		}
		pr_unlock(&lock);
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	(void)munmap((void *)vecfp_init, mmap_size);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
        { OPT_vecfp_method,	stress_set_vecfp_method },
};

stressor_info_t stress_vecfp_info = {
	.stressor = stress_vecfp,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
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
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
#endif
