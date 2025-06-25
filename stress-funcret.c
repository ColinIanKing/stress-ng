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
 *  Otherwise for s390 assume no decimal
 *  floating point is supported as the
 *  least risky default
 */
#undef HAVE_Decimal32
#undef HAVE_Decimal64
#undef HAVE_Decimal128
#endif
#endif

typedef bool (*stress_funcret_func)(stress_args_t *args);

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

#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal32)
stress_funcret_deep1(_Decimal32)
stress_funcret_deeper1(_Decimal32)
#endif

#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal64)
stress_funcret_deep1(_Decimal64)
stress_funcret_deeper1(_Decimal64)
#endif

#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Decimal128)
stress_funcret_deep1(_Decimal128)
stress_funcret_deeper1(_Decimal128)
#endif

#if defined(HAVE_fp16) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__fp16)
stress_funcret_deep1(__fp16)
stress_funcret_deeper1(__fp16)
#elif defined(HAVE_Float16) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Float16)
stress_funcret_deep1(_Float16)
stress_funcret_deeper1(_Float16)
#endif

#if defined(HAVE__float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float32)
stress_funcret_deep1(__float32)
stress_funcret_deeper1(__float32)
#elif defined(HAVE_Float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Float32)
stress_funcret_deep1(_Float32)
stress_funcret_deeper1(_Float32)
#endif

#if defined(HAVE__float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float64)
stress_funcret_deep1(__float64)
stress_funcret_deeper1(__float64)
#elif defined(HAVE_Float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Float64)
stress_funcret_deep1(_Float64)
stress_funcret_deeper1(_Float64)
#endif

#if defined(HAVE__float80) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float80)
stress_funcret_deep1(__float80)
stress_funcret_deeper1(__float80)
#endif

#if defined(HAVE__float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(__float128)
stress_funcret_deep1(__float128)
stress_funcret_deeper1(__float128)
#elif defined(HAVE_Float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret1(_Float128)
stress_funcret_deep1(_Float128)
stress_funcret_deeper1(_Float128)
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
#define cmp_mem(a, b, type)	shim_memcmp(&a.data, &b.data, sizeof(a.data))
#define cmp_type(a, b, type)	(a != b)
#define cmp_fp(a, b, type)	((a - b) > (type)0.0001)

#define stress_funcret_type(type, cmp)					\
static bool NOINLINE stress_funcret_ ## type(stress_args_t *args);\
									\
static bool NOINLINE stress_funcret_ ## type(stress_args_t *args)	\
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

#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal32, cmp_fp)
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal64, cmp_fp)
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal128, cmp_fp)
#endif
#if defined(HAVE_fp16) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__fp16, cmp_fp)
#elif defined(HAVE_Float16) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float16, cmp_fp)
#endif
#if defined(HAVE__float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float32, cmp_fp)
#elif defined(HAVE_Float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float32, cmp_fp)
#endif
#if defined(HAVE__float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float64, cmp_fp)
#elif defined(HAVE_Float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float64, cmp_fp)
#endif
#if defined(HAVE__float80) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float80, cmp_fp)
#endif
#if defined(HAVE__float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float128, cmp_fp)
#elif defined(HAVE_Float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float128, cmp_fp)
#endif

stress_funcret_type(stress_uint8x32_t, cmp_mem)
stress_funcret_type(stress_uint8x128_t, cmp_mem)
stress_funcret_type(stress_uint64x128_t, cmp_mem)

static bool stress_funcret_all(stress_args_t *args);

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
#if defined(HAVE_fp16) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float16",	stress_funcret___fp16 },
#elif defined(HAVE_Float16) && \
    !defined(HAVE_COMPILER_CLANG)
	{ "float16",	stress_funcret__Float16 },
#endif
#if defined(HAVE__float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float32",	stress_funcret___float32 },
#elif defined(HAVE_Float32) && \
    !defined(HAVE_COMPILER_CLANG)
	{ "float32",	stress_funcret__Float32 },
#endif
#if defined(HAVE__float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float64",	stress_funcret___float64 },
#elif defined(HAVE_Float64) && \
    !defined(HAVE_COMPILER_CLANG)
	{ "float64",	stress_funcret__Float64 },
#endif
#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float80",	stress_funcret___float80 },
#endif
#if defined(HAVE__float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",	stress_funcret___float128 },
#elif defined(HAVE_Float128) && \
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",	stress_funcret__Float128 },
#endif
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal32",	stress_funcret__Decimal32 },
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal64",	stress_funcret__Decimal64 },
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal128",	stress_funcret__Decimal128 },
#endif
	{ "uint8x32",	stress_funcret_stress_uint8x32_t },
	{ "uint8x128",	stress_funcret_stress_uint8x128_t },
	{ "uint64x128",	stress_funcret_stress_uint64x128_t },
};

#define NUM_STRESS_FUNCRET_METHODS	(SIZEOF_ARRAY(stress_funcret_methods))

static stress_metrics_t stress_funcret_metrics[NUM_STRESS_FUNCRET_METHODS];

static bool stress_funcret_exercise(stress_args_t *args, const size_t method)
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

static bool stress_funcret_all(stress_args_t *args)
{
	size_t i;
	bool success = true;

	for (i = 1; success && (i < NUM_STRESS_FUNCRET_METHODS); i++) {
		success &= stress_funcret_exercise(args, i);
	}
	return success;
}

/*
 *  stress_funcret()
 *	stress various argument sized function calls
 */
static int stress_funcret(stress_args_t *args)
{
	bool success = true;
	size_t funcret_method = 0;
	size_t i, j;

	(void)stress_get_setting("funcret-method", &funcret_method);

	stress_zero_metrics(stress_funcret_metrics, NUM_STRESS_FUNCRET_METHODS);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		success = stress_funcret_exercise(args, funcret_method);
	} while (success && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < NUM_STRESS_FUNCRET_METHODS; i++) {
		const double rate = (stress_funcret_metrics[i].duration > 0) ?
			stress_funcret_metrics[i].count / stress_funcret_metrics[i].duration : 0.0;

		if (rate > 0.0) {
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s function invocations per sec",
					stress_funcret_methods[i].name);
			stress_metrics_set(args, j, msg,
				rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const char *stress_funcret_method(const size_t i)
{
	return (i < NUM_STRESS_FUNCRET_METHODS) ? stress_funcret_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_funcret_method, "funcret_method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_funcret_method },
	END_OPT,
};

const stressor_info_t stress_funcret_info = {
	.stressor = stress_funcret,
	.classifier = CLASS_CPU,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
