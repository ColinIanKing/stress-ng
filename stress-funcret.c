// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
 *
 */
#include "stress-ng.h"
#include "core-arch.h"
#include "core-builtin.h"
#include "core-pragma.h"

#if defined(HAVE_COMPLEX_H)
#include <complex.h>
#endif

#if defined(STRESS_ARCH_S390)
/*
 *  Use decimal floating point for s390
 *  as some cpus don't support hard decimal
 *  floating point
 */
#if defined(STRESS_PRAGMA_NO_HARD_DFP)
STRESS_PRAGMA_NO_HARD_DFP
#else
/*
 *  Otheriwse for s390 assume no decimal
 *  floating point is supported as the
 *  least risky default
 */
#undef HAVE_FLOAT_DECIMAL32
#undef HAVE_FLOAT_DECIMAL64
#undef HAVE_FLOAT_DECIMAL128
#endif
#endif

typedef bool (*stress_funcret_func)(const stress_args_t *args);

typedef struct {
	const char *name;		/* human readable form of stressor */
	const stress_funcret_func func;	/* the funcret method function */
} stress_funcret_method_info_t;

static const stress_funcret_method_info_t stress_funcret_methods[];

static const stress_help_t help[] = {
	{ NULL,	"funcret N",		"start N workers exercising function return copying" },
	{ NULL,	"funcret-method M",	"select method of exercising a function return type" },
	{ NULL,	"funcret-ops N",	"stop after N function return bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef long double 	stress_long_double_t;

typedef struct {
	uint8_t		data[32];	/* cppcheck-suppress unusedStructMember */
} stress_uint8x32_t;

typedef struct {
	uint8_t		data[128];	/* cppcheck-suppress unusedStructMember */
} stress_uint8x128_t;

typedef struct {
	uint64_t	data[128];	/* cppcheck-suppress unusedStructMember */
} stress_uint64x128_t;

#define stress_funcret1(type)					\
static type NOINLINE stress_funcret_ ## type ## 1(type a);	\
static type NOINLINE stress_funcret_ ## type ## 1(type a)	\
{								\
	type b;							\
								\
	(void)shim_memcpy(&b, &a, sizeof(a));			\
	(void)shim_memset(&a, 0, sizeof(a));			\
	return b;						\
}								\

#define stress_funcret_deep1(type)				\
static type NOINLINE stress_funcret_deep_ ## type ## 1(type a);	\
								\
static type NOINLINE stress_funcret_deep_ ## type ## 1(type a)	\
{								\
	type b;							\
								\
	(void)shim_memcpy(&b, &a, sizeof(a));			\
	(void)shim_memset(&a, 0, sizeof(a));			\
	return stress_funcret_ ## type ## 1(b);			\
}								\

#define stress_funcret_deeper1(type)				\
static type NOINLINE stress_funcret_deeper_ ## type ## 1(type a);\
								\
static type NOINLINE stress_funcret_deeper_ ## type ## 1(type a)\
{								\
	type b;							\
								\
	(void)shim_memcpy(&b, &a, sizeof(a));			\
	(void)shim_memset(&a, 0, sizeof(a));			\
								\
	return stress_funcret_deep_ ## type ## 1(		\
		stress_funcret_ ## type ## 1(b));		\
}

stress_funcret1(uint8_t)
stress_funcret_deep1(uint8_t)
stress_funcret_deeper1(uint8_t)

stress_funcret1(uint16_t)
stress_funcret_deep1(uint16_t)
stress_funcret_deeper1(uint16_t)

stress_funcret1(uint32_t)
stress_funcret_deep1(uint32_t)
stress_funcret_deeper1(uint32_t)

stress_funcret1(uint64_t)
stress_funcret_deep1(uint64_t)
stress_funcret_deeper1(uint64_t)

#if defined(HAVE_INT128_T)
stress_funcret1(__uint128_t)
stress_funcret_deep1(__uint128_t)
stress_funcret_deeper1(__uint128_t)
#endif

stress_funcret1(float)
stress_funcret_deep1(float)
stress_funcret_deeper1(float)

stress_funcret1(double)
stress_funcret_deep1(double)
stress_funcret_deeper1(double)

stress_funcret1(stress_long_double_t)
stress_funcret_deep1(stress_long_double_t)
stress_funcret_deeper1(stress_long_double_t)

#if defined(HAVE_FLOAT_DECIMAL32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal32)
stress_funcret_deep1(_Decimal32)
stress_funcret_deeper1(_Decimal32)
#endif

#if defined(HAVE_FLOAT_DECIMAL64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal64)
stress_funcret_deep1(_Decimal64)
stress_funcret_deeper1(_Decimal64)
#endif

#if defined(HAVE_FLOAT_DECIMAL128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal128)
stress_funcret_deep1(_Decimal128)
stress_funcret_deeper1(_Decimal128)
#endif

#if defined(HAVE_FLOAT80) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float80)
stress_funcret_deep1(__float80)
stress_funcret_deeper1(__float80)
#endif

#if defined(HAVE_FLOAT128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float128)
stress_funcret_deep1(__float128)
stress_funcret_deeper1(__float128)
#endif

stress_funcret1(stress_uint8x32_t)
stress_funcret_deep1(stress_uint8x32_t)
stress_funcret_deeper1(stress_uint8x32_t)

stress_funcret1(stress_uint8x128_t)
stress_funcret_deep1(stress_uint8x128_t)
stress_funcret_deeper1(stress_uint8x128_t)

stress_funcret1(stress_uint64x128_t)
stress_funcret_deep1(stress_uint64x128_t)
stress_funcret_deeper1(stress_uint64x128_t)

static void stress_funcret_setvar(void *ptr, const size_t size)
{
	register size_t i;
	register uint8_t *ptr8 = (uint8_t *)ptr;

	for (i = 0; i < size; i++)
		ptr8[i] = stress_mwc8();
}

/*
 *  comparison functions, large values use memcmp, simple integer types
 *  use direct comparison, floating pointing use precision as equal values
 *  should never been compared for floating point
 */
#define cmp_mem(a, b, type)	shim_memcmp(&a, &b, sizeof(a))
#define cmp_type(a, b, type)	(a != b)
#define cmp_fp(a, b, type)	((a - b) > (type)0.0001)

#define stress_funcret_type(type, cmp)					\
static bool NOINLINE stress_funcret_ ## type(const stress_args_t *args);\
									\
static bool NOINLINE stress_funcret_ ## type(const stress_args_t *args)	\
{									\
	register size_t i;						\
	type a, old_b;							\
									\
	stress_funcret_setvar(&a, sizeof(a));				\
	/* taint old_b to keep old compilers happy */			\
	(void)shim_memcpy(&old_b, &a, sizeof(old_b));			\
									\
	for (i = 0; i < 1000; i++) {					\
		type b;							\
									\
		a = stress_funcret_ ## type ## 1(a);			\
		a = stress_funcret_deep_ ## type ## 1(a);		\
		a = stress_funcret_deeper_ ## type ## 1(a);		\
		b = a;							\
		if (i == 0) {						\
			old_b = b;					\
		} else {						\
			if (cmp(old_b, b, type)) 			\
				return false;				\
		}							\
	}								\
	stress_bogo_inc(args);						\
									\
	return true;							\
}

stress_funcret_type(uint8_t, cmp_type)
stress_funcret_type(uint16_t, cmp_type)
stress_funcret_type(uint32_t, cmp_type)
stress_funcret_type(uint64_t, cmp_type)
#if defined(HAVE_INT128_T)
stress_funcret_type(__uint128_t, cmp_type)
#endif
stress_funcret_type(float, cmp_fp)
stress_funcret_type(double, cmp_fp)

stress_funcret_type(stress_long_double_t, cmp_fp)

#if defined(HAVE_FLOAT_DECIMAL32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal32, cmp_fp)
#endif
#if defined(HAVE_FLOAT_DECIMAL64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal64, cmp_fp)
#endif
#if defined(HAVE_FLOAT_DECIMAL128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal128, cmp_fp)
#endif
#if defined(HAVE_FLOAT80) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float80, cmp_fp)
#endif
#if defined(HAVE_FLOAT128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float128, cmp_fp)
#endif

stress_funcret_type(stress_uint8x32_t, cmp_mem)
stress_funcret_type(stress_uint8x128_t, cmp_mem)
stress_funcret_type(stress_uint64x128_t, cmp_mem)

static bool stress_funcret_all(const stress_args_t *args);

/*
 * Table of func call stress methods
 */
static const stress_funcret_method_info_t stress_funcret_methods[] = {
	{ "all",	stress_funcret_all },
	{ "uint8",	stress_funcret_uint8_t },
	{ "uint16",	stress_funcret_uint16_t },
	{ "uint32",	stress_funcret_uint32_t },
	{ "uint64",	stress_funcret_uint64_t },
#if defined(HAVE_INT128_T)
	{ "uint128",	stress_funcret___uint128_t },
#endif
	{ "float",	stress_funcret_float },
	{ "double",	stress_funcret_double },
	{ "longdouble",	stress_funcret_stress_long_double_t },
#if defined(HAVE_FLOAT80) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float80",	stress_funcret___float80 },
#endif
#if defined(HAVE_FLOAT128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",	stress_funcret___float128 },
#endif
#if defined(HAVE_FLOAT_DECIMAL32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal32",	stress_funcret__Decimal32 },
#endif
#if defined(HAVE_FLOAT_DECIMAL64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal64",	stress_funcret__Decimal64 },
#endif
#if defined(HAVE_FLOAT_DECIMAL128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal128",	stress_funcret__Decimal128 },
#endif
	{ "uint8x32",	stress_funcret_stress_uint8x32_t },
	{ "uint8x128",	stress_funcret_stress_uint8x128_t },
	{ "uint64x128",	stress_funcret_stress_uint64x128_t },
};

static stress_metrics_t stress_funcret_metrics[SIZEOF_ARRAY(stress_funcret_methods)];

static bool stress_funcret_exercise(const stress_args_t *args, const size_t method)
{
	bool success;
	double t;

	t = stress_time_now();
	success = stress_funcret_methods[method].func(args);
	stress_funcret_metrics[method].duration += stress_time_now() - t;
	stress_funcret_metrics[method].count += 1.0;

	if (!success && (method != 0)) {
		pr_fail("%s: verification failed with a %s function call return value\n",
			args->name, stress_funcret_methods[method].name);
	}
	return success;
}

static bool stress_funcret_all(const stress_args_t *args)
{
	size_t i;
	bool success = true;

	for (i = 1; success && (i < SIZEOF_ARRAY(stress_funcret_methods)); i++) {
		success &= stress_funcret_exercise(args, i);
	}
	return success;
}

/*
 *  stress_set_funcret_method()
 *	set the default funccal stress method
 */
static int stress_set_funcret_method(const char *name)
{
	size_t i;

	for (i = 0; i < SIZEOF_ARRAY(stress_funcret_methods); i++) {
		if (!strcmp(stress_funcret_methods[i].name, name)) {
			stress_set_setting("funcret-method", TYPE_ID_SIZE_T, &i);
			return 0;
		}
	}

	(void)fprintf(stderr, "funcret-method must be one of:");
	for (i = 0; i < SIZEOF_ARRAY(stress_funcret_methods); i++) {
		(void)fprintf(stderr, " %s", stress_funcret_methods[i].name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_funcret()
 *	stress various argument sized function calls
 */
static int stress_funcret(const stress_args_t *args)
{
	bool success = true;
	size_t funcret_method = 0;
	size_t i, j;

	(void)stress_get_setting("funcret-method", &funcret_method);

	for (i = 0; i < SIZEOF_ARRAY(stress_funcret_metrics); i++) {
		stress_funcret_metrics[i].duration = 0.0;
		stress_funcret_metrics[i].count = 0.0;
	}

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		success = stress_funcret_exercise(args, funcret_method);
	} while (success && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < SIZEOF_ARRAY(stress_funcret_metrics); i++) {
		double rate = (stress_funcret_metrics[i].duration > 0) ?
			stress_funcret_metrics[i].count / stress_funcret_metrics[i].duration : 0.0;

		if (rate > 0.0) {
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s function invocations per sec",
					stress_funcret_methods[i].name);
			stress_metrics_set(args, j, msg, rate);
			j++;
		}
	}

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	{ OPT_funcret_method,	stress_set_funcret_method },
	{ 0,			NULL }
};

stressor_info_t stress_funcret_info = {
	.stressor = stress_funcret,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.verify = VERIFY_ALWAYS,
	.help = help
};
