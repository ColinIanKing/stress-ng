/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2026 Colin Ian King.
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
#include <math.h>

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

static const stress_help_t help[] = {
	{ NULL,	"funcret N",		"start N workers exercising function return copying" },
	{ NULL,	"funcret-method M",	"select method of exercising a function return type" },
	{ NULL,	"funcret-ops N",	"stop after N function return bogo operations" },
	{ NULL,	NULL,			NULL }
};

typedef long double 	stress_long_double_t;

#if defined(HAVE_COMPLEX_H)
typedef complex float		stress_complex_float_t;
typedef complex double		stress_complex_double_t;
typedef complex long double	stress_complex_long_double_t;
#endif

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

#if defined(HAVE_COMPLEX_H)
stress_funcret1(stress_complex_float_t)
stress_funcret_deep1(stress_complex_float_t)
stress_funcret_deeper1(stress_complex_float_t)

stress_funcret1(stress_complex_double_t)
stress_funcret_deep1(stress_complex_double_t)
stress_funcret_deeper1(stress_complex_double_t)

stress_funcret1(stress_complex_long_double_t)
stress_funcret_deep1(stress_complex_long_double_t)
stress_funcret_deeper1(stress_complex_long_double_t)
#endif

static void stress_funcret_setvar_mem(void *ptr, const size_t size)
{
	register size_t i;
	register uint8_t *ptr8 = (uint8_t *)ptr;

	for (i = 0; i < size; i++)
		ptr8[i] = stress_mwc8();
}

static void stress_funcret_setvar_fpf(void *ptr, const size_t size)
{
	(void)size;
	*(float *)ptr = (float)stress_mwc32() / (float)(1 + stress_mwc32());
}

static void stress_funcret_setvar_fpd(void *ptr, const size_t size)
{
	(void)size;
	*(double *)ptr = (double)stress_mwc32() / (double)(1 + stress_mwc32());
}

static void stress_funcret_setvar_fpl(void *ptr, const size_t size)
{
	(void)size;
	*(long double *)ptr = (long double)stress_mwc32() / (long double)(1 + stress_mwc32());
}

static void stress_funcret_setvar_cfpf(void *ptr, const size_t size)
{
	(void)size;
	*(complex float *)ptr = ((float)stress_mwc32() +
		I * (float)stress_mwc32()) / (float)(1 + stress_mwc32());
}

static void stress_funcret_setvar_cfpd(void *ptr, const size_t size)
{
	(void)size;
	*(complex double *)ptr = ((double)stress_mwc32() +
		I * (double)stress_mwc32()) / (double)(1 + stress_mwc32());
}

static void stress_funcret_setvar_cfpl(void *ptr, const size_t size)
{
	(void)size;
	*(complex long double *)ptr = ((long double)stress_mwc32() +
		I * (long double)stress_mwc32()) / (double)(1 + stress_mwc32());
}

#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_dfp32(void *ptr, const size_t size)
{
	(void)size;
	*(_Decimal32 *)ptr = (_Decimal32)stress_mwc32() /
			    ((_Decimal32)(1 + stress_mwc32()));
}
#endif

#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_dfp64(void *ptr, const size_t size)
{
	(void)size;
	*(_Decimal64 *)ptr = (_Decimal64)stress_mwc32() /
			    ((_Decimal64)(1 + stress_mwc32()));
}
#endif

#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_dfp128(void *ptr, const size_t size)
{
	(void)size;
	*(_Decimal128 *)ptr = (_Decimal128)stress_mwc32() /
			    ((_Decimal128)(1 + stress_mwc32()));
}
#endif

#if defined(HAVE_fp16) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_fp16(void *ptr, const size_t size)
{
	(void)size;
	*(__fp16 *)ptr = (__fp16)stress_mwc32() / (__fp16)(1 + stress_mwc32());
}
#elif defined(HAVE_Float16) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_Float16(void *ptr, const size_t size)
{
	(void)size;
	*(_Float16 *)ptr = (_Float16)stress_mwc32() / (_Float16)(1 + stress_mwc32());
}
#endif

#if defined(HAVE__float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_float32(void *ptr, const size_t size)
{
	(void)size;
	*(__float32 *)ptr = (__float32)stress_mwc32() / (__float32)(1 + stress_mwc32());
}
#elif defined(HAVE_Float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_Float32(void *ptr, const size_t size)
{
	(void)size;
	*(_Float32 *)ptr = (_Float32)stress_mwc32() / (_Float32)(1 + stress_mwc32());
}
#endif

#if defined(HAVE__float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_float64(void *ptr, const size_t size)
{
	(void)size;
	*(__float64 *)ptr = (__float64)stress_mwc32() / (__float64)(1 + stress_mwc32());
}
#elif defined(HAVE_Float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_Float64(void *ptr, const size_t size)
{
	(void)size;
	*(_Float64 *)ptr = (_Float64)stress_mwc32() / (_Float64)(1 + stress_mwc32());
}
#endif

#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_float80(void *ptr, const size_t size)
{
	(void)size;
	*(__float80 *)ptr = (__float80)stress_mwc32() / (__float80)(1 + stress_mwc32());
}
#endif

#if defined(HAVE__float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_float128(void *ptr, const size_t size)
{
	(void)size;
	*(__float128 *)ptr = (__float128)stress_mwc32() / (__float128)(1 + stress_mwc32());
}
#elif defined(HAVE_Float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
static void stress_funcret_setvar_Float128(void *ptr, const size_t size)
{
	(void)size;
	*(_Float128 *)ptr = (_Float128)stress_mwc32() / (_Float128)(1 + stress_mwc32());
}
#endif

/*
 *  comparison functions, large values use memcmp, simple integer types
 *  use direct comparison, floating pointing use precision as equal values
 *  should never been compared for floating point
 */
#define CMP_MEM(a, b, type)	shim_memcmp(&a.data, &b.data, sizeof(a.data))
#define CMP_TYPE(a, b, type)	(a != b)

static inline int cmp_fpf(const float a, const float b)
{
	const float diff  = fabsf(a - b);
	const float abs_a = fabsf(a);
	const float abs_b = fabsf(b);

	return diff > ((abs_a < abs_b ? abs_a : abs_b) * 0.0001L);
}

static inline int cmp_fpd(const double a, const double b)
{
	const double diff  = fabs(a - b);
	const double abs_a = fabs(a);
	const double abs_b = fabs(b);

	return diff > ((abs_a < abs_b ? abs_a : abs_b) * 0.0001L);
}

static inline int cmp_fpl(const long double a, const long double b)
{
	const long double diff  = fabsl(a - b);
	const long double abs_a = fabsl(a);
	const long double abs_b = fabsl(b);

	return diff > ((abs_a < abs_b ? abs_a : abs_b) * 0.0001L);
}

#define CMP_FPF(a, b, type)	cmp_fpf((float)a, (float)b)
#define CMP_FPD(a, b, type)	cmp_fpd((double)a, (double)b)
#define CMP_FPL(a, b, type)	cmp_fpl((long double)a, (long double)b)

/* maximum of two complex valus is the modulus */
#define CMAXF(x, y)		(float)csqrtf((x * x) + (y * y))
#define CMAXD(x, y)		(double)csqrt((x * x) + (y * y))
#define CMAXL(x, y)		(long double)csqrtl((x * x) + (y * y))

static inline int cmp_cfpf(const complex float a, const complex float b)
{
	return cabsf(a - b) > (0.0001f * CMAXF(a, b));
}

static inline int cmp_cfpd(const complex double a, const complex double b)
{
	return cabs(a - b) > (0.0001 * CMAXD(a, b));
}

static inline int cmp_cfpl(const complex long double a, const complex long double b)
{
	return cabsl(a - b) > (0.0001L * CMAXL(a, b));
}

#define CMP_CFPF(a, b, type)	cmp_cfpf((complex float)a, (complex float)b)
#define CMP_CFPD(a, b, type)	cmp_cfpd((complex double)a, (complex double)b)
#define CMP_CFPL(a, b, type)	cmp_cfpl((complex long double)a, (complex long double)b)

#define stress_funcret_type(type, cmp, set)				\
static bool NOINLINE stress_funcret_ ## type(stress_args_t *args);	\
									\
static bool NOINLINE stress_funcret_ ## type(stress_args_t *args)	\
{									\
	register size_t i;						\
	type a, old_b;							\
									\
	stress_funcret_setvar_ ## set(&a, sizeof(a));			\
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

stress_funcret_type(uint8_t, CMP_TYPE, mem)
stress_funcret_type(uint16_t, CMP_TYPE, mem)
stress_funcret_type(uint32_t, CMP_TYPE, mem)
stress_funcret_type(uint64_t, CMP_TYPE, mem)
#if defined(HAVE_INT128_T)
stress_funcret_type(__uint128_t, CMP_TYPE, mem)
#endif
stress_funcret_type(float, CMP_FPF, fpf)
stress_funcret_type(double, CMP_FPD, fpd)
stress_funcret_type(stress_long_double_t, CMP_FPL, fpl)

#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal32, CMP_FPF, dfp32)
#endif
#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal64, CMP_FPD, dfp64)
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Decimal128, CMP_FPL, dfp128)
#endif
#if defined(HAVE_fp16) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__fp16, CMP_FPF, fp16)
#elif defined(HAVE_Float16) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float16, CMP_FPF, Float16)
#endif
#if defined(HAVE__float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float32, CMP_FPD, float32)
#elif defined(HAVE_Float32) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float32, CMP_FPD, Float32)
#endif
#if defined(HAVE__float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float64, CMP_FPL, float64)
#elif defined(HAVE_Float64) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float64, CMP_FPL, Float64)
#endif
#if defined(HAVE__float80) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float80, CMP_FPL, float80)
#endif
#if defined(HAVE__float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(__float128, CMP_FPL, float128)
#elif defined(HAVE_Float128) &&		\
    !defined(HAVE_COMPILER_CLANG)
stress_funcret_type(_Float128, CMP_FPL, Float128)
#endif

stress_funcret_type(stress_uint8x32_t, CMP_MEM, mem)
stress_funcret_type(stress_uint8x128_t, CMP_MEM, mem)
stress_funcret_type(stress_uint64x128_t, CMP_MEM, mem)

#if defined(HAVE_COMPLEX_H)
stress_funcret_type(stress_complex_float_t, CMP_CFPF, cfpf)
stress_funcret_type(stress_complex_double_t, CMP_CFPD, cfpd)
stress_funcret_type(stress_complex_long_double_t, CMP_CFPL, cfpl)
#endif

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
#if defined(HAVE_COMPLEX_H)
	{ "cfloat",	stress_funcret_stress_complex_float_t },
	{ "cdouble",	stress_funcret_stress_complex_double_t },
	{ "clongdouble", stress_funcret_stress_complex_long_double_t },
#endif
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
	size_t i;

	(void)stress_setting_get("funcret-method", &funcret_method);

	stress_zero_metrics(stress_funcret_metrics, NUM_STRESS_FUNCRET_METHODS);

	stress_proc_state_set(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_proc_state_set(args->name, STRESS_STATE_RUN);

	do {
		success = stress_funcret_exercise(args, funcret_method);
	} while (success && stress_continue(args));

	stress_proc_state_set(args->name, STRESS_STATE_DEINIT);

	for (i = 1; i < NUM_STRESS_FUNCRET_METHODS; i++) {
		const double rate = (stress_funcret_metrics[i].duration > 0) ?
			stress_funcret_metrics[i].count / stress_funcret_metrics[i].duration : 0.0;

		if (rate > 0.0) {
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s function invocations per sec",
					stress_funcret_methods[i].name);
			stress_metrics_set(args, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
		}
	}

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const char *stress_funcret_method(const size_t i)
{
	return (i < NUM_STRESS_FUNCRET_METHODS) ? stress_funcret_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_funcret_method, "funcret-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_funcret_method },
	END_OPT,
};

static const stress_exercises_t exercises[] = {
	STRESS_EX_FEATURE("bogo-ops-stable"),
	STRESS_EX_FEATURE("d-tlb-read-miss"),
	STRESS_EX_FEATURE("cfp"),
#if (defined(HAVE_Decimal32) ||		\
     defined(HAVE_Decimal64) ||		\
     defined(HAVE_Decimal128)) &&	\
    !defined(HAVE_COMPILER_CLANG)
	STRESS_EX_FEATURE("fp-decimal"),
#endif

	STRESS_EX_FEATURE("fp"),
	STRESS_EX_FEATURE("integer"),
	STRESS_EX_FEATURE("memory-copy"),
	STRESS_EX_FEATURE("stack"),
	STRESS_EX_FEATURE("user-time"),

	STRESS_EX_END,
};

const stressor_info_t stress_funcret_info = {
	.stressor = stress_funcret,
	.classifier = CLASS_CPU,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help,
	.max_metrics_items = SIZEOF_ARRAY(stress_funcret_methods),
	.exercises = exercises,
};
