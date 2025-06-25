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

#include <math.h>

#if defined(STRESS_ARCH_S390)
#undef ALWAYS_INLINE
#define ALWAYS_INLINE
#endif

#include "core-put.h"

#if defined(STRESS_ARCH_SH4)
#undef HAVE_COMPLEX_H
#undef HAVE_COMPLEX
#endif

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

#if defined(STRESS_ARCH_SH4)
#undef HAVE_Decimal32
#undef HAVE_Decimal64
#undef HAVE_Decimal128
#endif

typedef bool (*stress_funccall_func)(stress_args_t *args);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_funccall_func   func;   /* the funccall method function */
} stress_funccall_method_info_t;

static const stress_funccall_method_info_t stress_funccall_methods[];

static const stress_help_t help[] = {
	{ NULL,	"funccall N",		"start N workers exercising 1 to 9 arg functions" },
	{ NULL,	"funccall-method M",	"select function call method M" },
	{ NULL,	"funccall-ops N",	"stop after N function call bogo operations" },
	{ NULL,	NULL,			NULL }
};

#define bool_put	stress_uint8_put
#define uint8_t_put	stress_uint8_put
#define uint16_t_put	stress_uint16_put
#define uint32_t_put	stress_uint32_put
#define uint64_t_put	stress_uint64_put
#define __uint128_t_put	stress_uint128_put
#define float_put	stress_float_put
#define double_put	stress_double_put
#define stress_long_double_t_put stress_long_double_put
typedef long double 		stress_long_double_t;
#if defined(HAVE_COMPLEX_H) &&			\
    defined(HAVE_COMPLEX) &&			\
    defined(__STDC_IEC_559_COMPLEX__) &&	\
    !defined(__UCLIBC__)

typedef complex float		stress_complex_float_t;
typedef complex double		stress_complex_double_t;
typedef complex long double	stress_complex_long_double_t;

static inline void stress_complex_float_t_put(stress_complex_float_t a)
{
#if defined(HAVE_CREALF) &&	\
    defined(HAVE_CIMAGF)
	g_put_val.float_val = crealf(a) * cimagf(a);
#else
	g_put_val.float_val = a * a;
#endif
}

static inline ALWAYS_INLINE void stress_complex_double_t_put(stress_complex_double_t a)
{
#if defined(HAVE_CREAL) &&	\
    defined(HAVE_CIMAG)
	g_put_val.double_val = creal(a) * cimag(a);
#else
	g_put_val.double_val = a * a;
#endif
}

static inline ALWAYS_INLINE void stress_complex_long_double_t_put(stress_complex_long_double_t a)
{
#if defined(HAVE_CREALL) &&	\
    defined(HAVE_CIMAGL)
	g_put_val.long_double_val = creall(a) * cimagl(a);
#else
	g_put_val.long_double_val = a * a;
#endif
}

#endif

static inline ALWAYS_INLINE float stress_mwcfloat(void)
{
	const uint32_t r = stress_mwc32();

	return (float)r / (float)(0xffffffffUL);
}

static inline double stress_mwcdouble(void)
{
	const uint64_t r = stress_mwc64();

	return (double)r / (double)(0xffffffffffffffffULL);
}

/*
 *  comparison functions, simple integer types use direct comparison,
 *  floating pointing use precision as equal values should never been
 *  compared for floating point
 */
#define cmp_type(a, b, type)	(a != b)
#define cmp_fp(a, b, type)	(fabs((double)(a - b)) > (type)0.0001)
#define cmp_cmplx(a, b, type)	(cabs((double complex)(a - b)) > (double)0.0001)
#define cmp_ignore(a, b, type)	(0)

#define stress_funccall_type(type, rndfunc, cmpfunc)			\
static bool NOINLINE 							\
stress_funccall_ ## type(stress_args_t *args);			\
									\
static bool NOINLINE							\
stress_funccall_ ## type(stress_args_t *args)			\
{									\
	register int ii;						\
	type res_old;							\
									\
	const type a = rndfunc();					\
	const type b = rndfunc();					\
	const type c = rndfunc();					\
	const type d = rndfunc();					\
	const type e = rndfunc();					\
	const type f = rndfunc();					\
	const type g = rndfunc();					\
	const type h = rndfunc();					\
	const type i = rndfunc();					\
									\
	(void)shim_memset(&res_old, 0, sizeof(res_old));		\
									\
	for (ii = 0; ii < 1000; ii++) {					\
		type res_new = 						\
		 stress_funccall_ ## type ## _1(a) + 			\
		 stress_funccall_ ## type ## _2(a, b) +			\
		 stress_funccall_ ## type ## _3(a, b, c) +		\
		 stress_funccall_ ## type ## _4(a, b, c, d) + 		\
		 stress_funccall_ ## type ## _5(a, b, c, d, e) +	\
		 stress_funccall_ ## type ## _6(a, b, c, d, e, f) + 	\
		 stress_funccall_ ## type ## _7(a, b, c, d, e, f, g) + 	\
		 stress_funccall_ ## type ## _8(a, b, c, d, e, f, g, h) + \
		 stress_funccall_ ## type ## _9(a, b, c, d, e, f, g, h, i); \
									\
		res_new += 						\
		 stress_funcdeep_ ## type ## _1(a) +			\
		 stress_funcdeep_ ## type ## _2(a, b) +			\
		 stress_funcdeep_ ## type ## _3(a, b, c) + 		\
		 stress_funcdeep_ ## type ## _4(a, b, c, d) +		\
		 stress_funcdeep_ ## type ## _5(a, b, c, d, e) +	\
		 stress_funcdeep_ ## type ## _6(a, b, c, d, e, f) +	\
		 stress_funcdeep_ ## type ## _7(a, b, c, d, e, f, g) +	\
		 stress_funcdeep_ ## type ## _8(a, b, c, d, e, f, g, h) +\
		 stress_funcdeep_ ## type ## _9(a, b, c, d, e, f, g, h, i); \
		type ## _put(res_new);					\
		if (ii == 0) {						\
			res_old = res_new;				\
		} else {						\
			if (cmpfunc(res_old, res_new, type)) {		\
				return false;				\
			}						\
		}							\
	}								\
	stress_bogo_inc(args);						\
	return true;							\
}

#define stress_funccall_1(type)				\
static type NOINLINE stress_funccall_ ## type ## _1(	\
	const type a);					\
							\
static type NOINLINE stress_funccall_ ## type ## _1(	\
	const type a)					\
{							\
	type ## _put(a);				\
	return a;					\
}							\

#define stress_funccall_2(type)				\
static type  NOINLINE stress_funccall_ ## type ## _2(	\
	const type a,					\
	const type b);					\
							\
static type NOINLINE stress_funccall_ ## type ## _2(	\
	const type a,					\
	const type b)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	return a + b;					\
}							\

#define stress_funccall_3(type)				\
static type NOINLINE stress_funccall_ ## type ## _3(	\
	const type a,					\
	const type b,					\
	const type c);					\
							\
static type NOINLINE stress_funccall_ ## type ## _3(	\
	const type a,					\
	const type b,					\
	const type c)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	return a + b + c;				\
}							\

#define stress_funccall_4(type)				\
static type NOINLINE stress_funccall_ ## type ## _4(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d);					\
							\
static type NOINLINE stress_funccall_ ## type ## _4(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	return a + b + c + d;				\
}							\

#define stress_funccall_5(type)				\
static type NOINLINE stress_funccall_ ## type ## _5(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e);					\
							\
static type NOINLINE stress_funccall_ ## type ## _5(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	type ## _put(e);				\
	return a + b + c + d + e;			\
}							\

#define stress_funccall_6(type)				\
static type NOINLINE stress_funccall_ ## type ## _6(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f);					\
							\
static type NOINLINE stress_funccall_ ## type ## _6(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	type ## _put(e);				\
	type ## _put(f);				\
	return a + b + c + d + e + f;			\
}							\

#define stress_funccall_7(type)				\
static type NOINLINE stress_funccall_ ## type ## _7(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g);					\
							\
static type NOINLINE stress_funccall_ ## type ## _7(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	type ## _put(e);				\
	type ## _put(f);				\
	type ## _put(g);				\
	return a + b + c + d + e + f + g;		\
}							\

#define stress_funccall_8(type)				\
static type NOINLINE stress_funccall_ ## type ## _8(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h);					\
							\
static type NOINLINE stress_funccall_ ## type ## _8(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	type ## _put(e);				\
	type ## _put(f);				\
	type ## _put(g);				\
	type ## _put(h);				\
	return a + b + c + d + e + f + g + h;		\
}							\

#define stress_funccall_9(type)				\
static type NOINLINE stress_funccall_ ## type ## _9(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h,					\
	const type i);					\
							\
static type NOINLINE stress_funccall_ ## type ## _9(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h,					\
	const type i)					\
{							\
	type ## _put(a);				\
	type ## _put(b);				\
	type ## _put(c);				\
	type ## _put(d);				\
	type ## _put(e);				\
	type ## _put(f);				\
	type ## _put(g);				\
	type ## _put(h);				\
	type ## _put(i);				\
	return a + b + c + d + e + f + g + h + i;	\
}							\

#define stress_funcdeep_1(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _1(	\
	const type a);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _1(	\
	const type a)					\
{							\
	return a;					\
}							\

#define stress_funcdeep_2(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _2(	\
	const type a,					\
	const type b);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _2(	\
	const type a,					\
	const type b)					\
{							\
	return						\
	stress_funccall_ ## type ## _1(b) + 		\
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_3(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _3(	\
	const type a,					\
	const type b,					\
	const type c);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _3(	\
	const type a,					\
	const type b,					\
	const type c)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _2(c, b) +		\
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_4(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _4(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _4(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _3(d, c, b) + 	\
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_5(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _5(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _5(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _4(e, d, c, b) + 	\
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_6(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _6(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _6(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _5(f, e, d, c, b) + \
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_7(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _7(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _7(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _6(g, f, e, d, c, b) + \
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_8(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _8(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _8(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h)					\
{							\
	return						\
	stress_funcdeep_ ## type ## _7(h, g, f, e, d, c, b) + \
	stress_funccall_ ## type ## _1(a);		\
}							\

#define stress_funcdeep_9(type)				\
static type NOINLINE stress_funcdeep_ ## type ## _9(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h,					\
	const type i);					\
							\
static type NOINLINE stress_funcdeep_ ## type ## _9(	\
	const type a,					\
	const type b,					\
	const type c,					\
	const type d,					\
	const type e,					\
	const type f,					\
	const type g,					\
	const type h,					\
	const type i)					\
{							\
	return						\
	stress_funccall_ ## type ## _1(b) + 		\
	stress_funcdeep_ ## type ## _2(c, b) + 		\
	stress_funcdeep_ ## type ## _3(d, c, b) + 	\
	stress_funcdeep_ ## type ## _4(e, d, c, b) + 	\
	stress_funcdeep_ ## type ## _5(f, e, d, c, b) + \
	stress_funcdeep_ ## type ## _6(g, f, e, d, c, b) + \
	stress_funcdeep_ ## type ## _7(h, g, f, e, d, c, b) + \
	stress_funcdeep_ ## type ## _8(i, h, g, f, e, d, c, b) + \
	stress_funcdeep_ ## type ## _8(a, b, c, d, e, f, g, h) + \
	stress_funcdeep_ ## type ## _7(b, c, d, e, f, g, h) + \
	stress_funcdeep_ ## type ## _6(c, d, e, f, g, h) + \
	stress_funcdeep_ ## type ## _5(d, e, f, g, h) + \
	stress_funcdeep_ ## type ## _4(e, f, g, h) + 	\
	stress_funcdeep_ ## type ## _3(f, g, h) + 	\
	stress_funcdeep_ ## type ## _2(g, h) +		\
	stress_funccall_ ## type ## _1(h);		\
}							\

stress_funccall_1(bool)
stress_funccall_2(bool)
stress_funccall_3(bool)
stress_funccall_4(bool)
stress_funccall_5(bool)
stress_funccall_6(bool)
stress_funccall_7(bool)
stress_funccall_8(bool)
stress_funccall_9(bool)
stress_funcdeep_1(bool)
stress_funcdeep_2(bool)
stress_funcdeep_3(bool)
stress_funcdeep_4(bool)
stress_funcdeep_5(bool)
stress_funcdeep_6(bool)
stress_funcdeep_7(bool)
stress_funcdeep_8(bool)
stress_funcdeep_9(bool)
stress_funccall_type(bool, stress_mwc1, cmp_type)

stress_funccall_1(uint8_t)
stress_funccall_2(uint8_t)
stress_funccall_3(uint8_t)
stress_funccall_4(uint8_t)
stress_funccall_5(uint8_t)
stress_funccall_6(uint8_t)
stress_funccall_7(uint8_t)
stress_funccall_8(uint8_t)
stress_funccall_9(uint8_t)
stress_funcdeep_1(uint8_t)
stress_funcdeep_2(uint8_t)
stress_funcdeep_3(uint8_t)
stress_funcdeep_4(uint8_t)
stress_funcdeep_5(uint8_t)
stress_funcdeep_6(uint8_t)
stress_funcdeep_7(uint8_t)
stress_funcdeep_8(uint8_t)
stress_funcdeep_9(uint8_t)
stress_funccall_type(uint8_t, stress_mwc8, cmp_type)

stress_funccall_1(uint16_t)
stress_funccall_2(uint16_t)
stress_funccall_3(uint16_t)
stress_funccall_4(uint16_t)
stress_funccall_5(uint16_t)
stress_funccall_6(uint16_t)
stress_funccall_7(uint16_t)
stress_funccall_8(uint16_t)
stress_funccall_9(uint16_t)
stress_funcdeep_1(uint16_t)
stress_funcdeep_2(uint16_t)
stress_funcdeep_3(uint16_t)
stress_funcdeep_4(uint16_t)
stress_funcdeep_5(uint16_t)
stress_funcdeep_6(uint16_t)
stress_funcdeep_7(uint16_t)
stress_funcdeep_8(uint16_t)
stress_funcdeep_9(uint16_t)
stress_funccall_type(uint16_t, stress_mwc16, cmp_type)

stress_funccall_1(uint32_t)
stress_funccall_2(uint32_t)
stress_funccall_3(uint32_t)
stress_funccall_4(uint32_t)
stress_funccall_5(uint32_t)
stress_funccall_6(uint32_t)
stress_funccall_7(uint32_t)
stress_funccall_8(uint32_t)
stress_funccall_9(uint32_t)
stress_funcdeep_1(uint32_t)
stress_funcdeep_2(uint32_t)
stress_funcdeep_3(uint32_t)
stress_funcdeep_4(uint32_t)
stress_funcdeep_5(uint32_t)
stress_funcdeep_6(uint32_t)
stress_funcdeep_7(uint32_t)
stress_funcdeep_8(uint32_t)
stress_funcdeep_9(uint32_t)
stress_funccall_type(uint32_t, stress_mwc32, cmp_type)

stress_funccall_1(uint64_t)
stress_funccall_2(uint64_t)
stress_funccall_3(uint64_t)
stress_funccall_4(uint64_t)
stress_funccall_5(uint64_t)
stress_funccall_6(uint64_t)
stress_funccall_7(uint64_t)
stress_funccall_8(uint64_t)
stress_funccall_9(uint64_t)
stress_funcdeep_1(uint64_t)
stress_funcdeep_2(uint64_t)
stress_funcdeep_3(uint64_t)
stress_funcdeep_4(uint64_t)
stress_funcdeep_5(uint64_t)
stress_funcdeep_6(uint64_t)
stress_funcdeep_7(uint64_t)
stress_funcdeep_8(uint64_t)
stress_funcdeep_9(uint64_t)
stress_funccall_type(uint64_t, stress_mwc64, cmp_type)

#if defined(HAVE_INT128_T)
stress_funccall_1(__uint128_t)
stress_funccall_2(__uint128_t)
stress_funccall_3(__uint128_t)
stress_funccall_4(__uint128_t)
stress_funccall_5(__uint128_t)
stress_funccall_6(__uint128_t)
stress_funccall_7(__uint128_t)
stress_funccall_8(__uint128_t)
stress_funccall_9(__uint128_t)
stress_funcdeep_1(__uint128_t)
stress_funcdeep_2(__uint128_t)
stress_funcdeep_3(__uint128_t)
stress_funcdeep_4(__uint128_t)
stress_funcdeep_5(__uint128_t)
stress_funcdeep_6(__uint128_t)
stress_funcdeep_7(__uint128_t)
stress_funcdeep_8(__uint128_t)
stress_funcdeep_9(__uint128_t)
stress_funccall_type(__uint128_t, stress_mwc64, cmp_type)
#endif

stress_funccall_1(float)
stress_funccall_2(float)
stress_funccall_3(float)
stress_funccall_4(float)
stress_funccall_5(float)
stress_funccall_6(float)
stress_funccall_7(float)
stress_funccall_8(float)
stress_funccall_9(float)
stress_funcdeep_1(float)
stress_funcdeep_2(float)
stress_funcdeep_3(float)
stress_funcdeep_4(float)
stress_funcdeep_5(float)
stress_funcdeep_6(float)
stress_funcdeep_7(float)
stress_funcdeep_8(float)
stress_funcdeep_9(float)
stress_funccall_type(float, stress_mwcfloat, cmp_fp)

stress_funccall_1(double)
stress_funccall_2(double)
stress_funccall_3(double)
stress_funccall_4(double)
stress_funccall_5(double)
stress_funccall_6(double)
stress_funccall_7(double)
stress_funccall_8(double)
stress_funccall_9(double)
stress_funcdeep_1(double)
stress_funcdeep_2(double)
stress_funcdeep_3(double)
stress_funcdeep_4(double)
stress_funcdeep_5(double)
stress_funcdeep_6(double)
stress_funcdeep_7(double)
stress_funcdeep_8(double)
stress_funcdeep_9(double)
stress_funccall_type(double, stress_mwcdouble, cmp_fp)

stress_funccall_1(stress_long_double_t)
stress_funccall_2(stress_long_double_t)
stress_funccall_3(stress_long_double_t)
stress_funccall_4(stress_long_double_t)
stress_funccall_5(stress_long_double_t)
stress_funccall_6(stress_long_double_t)
stress_funccall_7(stress_long_double_t)
stress_funccall_8(stress_long_double_t)
stress_funccall_9(stress_long_double_t)
stress_funcdeep_1(stress_long_double_t)
stress_funcdeep_2(stress_long_double_t)
stress_funcdeep_3(stress_long_double_t)
stress_funcdeep_4(stress_long_double_t)
stress_funcdeep_5(stress_long_double_t)
stress_funcdeep_6(stress_long_double_t)
stress_funcdeep_7(stress_long_double_t)
stress_funcdeep_8(stress_long_double_t)
stress_funcdeep_9(stress_long_double_t)
stress_funccall_type(stress_long_double_t, stress_mwc64, cmp_fp)

#if defined(HAVE_COMPLEX_H) &&			\
    defined(HAVE_COMPLEX) &&			\
    defined(__STDC_IEC_559_COMPLEX__) &&	\
    !defined(__UCLIBC__)

stress_funccall_1(stress_complex_float_t)
stress_funccall_2(stress_complex_float_t)
stress_funccall_3(stress_complex_float_t)
stress_funccall_4(stress_complex_float_t)
stress_funccall_5(stress_complex_float_t)
stress_funccall_6(stress_complex_float_t)
stress_funccall_7(stress_complex_float_t)
stress_funccall_8(stress_complex_float_t)
stress_funccall_9(stress_complex_float_t)
stress_funcdeep_1(stress_complex_float_t)
stress_funcdeep_2(stress_complex_float_t)
stress_funcdeep_3(stress_complex_float_t)
stress_funcdeep_4(stress_complex_float_t)
stress_funcdeep_5(stress_complex_float_t)
stress_funcdeep_6(stress_complex_float_t)
stress_funcdeep_7(stress_complex_float_t)
stress_funcdeep_8(stress_complex_float_t)
stress_funcdeep_9(stress_complex_float_t)
stress_funccall_type(stress_complex_float_t, stress_mwcdouble, cmp_cmplx)

stress_funccall_1(stress_complex_double_t)
stress_funccall_2(stress_complex_double_t)
stress_funccall_3(stress_complex_double_t)
stress_funccall_4(stress_complex_double_t)
stress_funccall_5(stress_complex_double_t)
stress_funccall_6(stress_complex_double_t)
stress_funccall_7(stress_complex_double_t)
stress_funccall_8(stress_complex_double_t)
stress_funccall_9(stress_complex_double_t)
stress_funcdeep_1(stress_complex_double_t)
stress_funcdeep_2(stress_complex_double_t)
stress_funcdeep_3(stress_complex_double_t)
stress_funcdeep_4(stress_complex_double_t)
stress_funcdeep_5(stress_complex_double_t)
stress_funcdeep_6(stress_complex_double_t)
stress_funcdeep_7(stress_complex_double_t)
stress_funcdeep_8(stress_complex_double_t)
stress_funcdeep_9(stress_complex_double_t)
stress_funccall_type(stress_complex_double_t, stress_mwcdouble, cmp_cmplx)

stress_funccall_1(stress_complex_long_double_t)
stress_funccall_2(stress_complex_long_double_t)
stress_funccall_3(stress_complex_long_double_t)
stress_funccall_4(stress_complex_long_double_t)
stress_funccall_5(stress_complex_long_double_t)
stress_funccall_6(stress_complex_long_double_t)
stress_funccall_7(stress_complex_long_double_t)
stress_funccall_8(stress_complex_long_double_t)
stress_funccall_9(stress_complex_long_double_t)
stress_funcdeep_1(stress_complex_long_double_t)
stress_funcdeep_2(stress_complex_long_double_t)
stress_funcdeep_3(stress_complex_long_double_t)
stress_funcdeep_4(stress_complex_long_double_t)
stress_funcdeep_5(stress_complex_long_double_t)
stress_funcdeep_6(stress_complex_long_double_t)
stress_funcdeep_7(stress_complex_long_double_t)
stress_funcdeep_8(stress_complex_long_double_t)
stress_funcdeep_9(stress_complex_long_double_t)
stress_funccall_type(stress_complex_long_double_t, stress_mwcdouble, cmp_cmplx)
#endif

#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Decimal32_put(const _Decimal32 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(_Decimal32)
stress_funccall_2(_Decimal32)
stress_funccall_3(_Decimal32)
stress_funccall_4(_Decimal32)
stress_funccall_5(_Decimal32)
stress_funccall_6(_Decimal32)
stress_funccall_7(_Decimal32)
stress_funccall_8(_Decimal32)
stress_funccall_9(_Decimal32)
stress_funcdeep_1(_Decimal32)
stress_funcdeep_2(_Decimal32)
stress_funcdeep_3(_Decimal32)
stress_funcdeep_4(_Decimal32)
stress_funcdeep_5(_Decimal32)
stress_funcdeep_6(_Decimal32)
stress_funcdeep_7(_Decimal32)
stress_funcdeep_8(_Decimal32)
stress_funcdeep_9(_Decimal32)
stress_funccall_type(_Decimal32, (_Decimal32)stress_mwc64, cmp_ignore)
#endif

#if defined(HAVE_Decimal64) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Decimal64_put(const _Decimal64 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(_Decimal64)
stress_funccall_2(_Decimal64)
stress_funccall_3(_Decimal64)
stress_funccall_4(_Decimal64)
stress_funccall_5(_Decimal64)
stress_funccall_6(_Decimal64)
stress_funccall_7(_Decimal64)
stress_funccall_8(_Decimal64)
stress_funccall_9(_Decimal64)
stress_funcdeep_1(_Decimal64)
stress_funcdeep_2(_Decimal64)
stress_funcdeep_3(_Decimal64)
stress_funcdeep_4(_Decimal64)
stress_funcdeep_5(_Decimal64)
stress_funcdeep_6(_Decimal64)
stress_funcdeep_7(_Decimal64)
stress_funcdeep_8(_Decimal64)
stress_funcdeep_9(_Decimal64)
stress_funccall_type(_Decimal64, (_Decimal64)stress_mwc64, cmp_ignore)
#endif

#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Decimal128_put(const _Decimal128 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(_Decimal128)
stress_funccall_2(_Decimal128)
stress_funccall_3(_Decimal128)
stress_funccall_4(_Decimal128)
stress_funccall_5(_Decimal128)
stress_funccall_6(_Decimal128)
stress_funccall_7(_Decimal128)
stress_funccall_8(_Decimal128)
stress_funccall_9(_Decimal128)
stress_funcdeep_1(_Decimal128)
stress_funcdeep_2(_Decimal128)
stress_funcdeep_3(_Decimal128)
stress_funcdeep_4(_Decimal128)
stress_funcdeep_5(_Decimal128)
stress_funcdeep_6(_Decimal128)
stress_funcdeep_7(_Decimal128)
stress_funcdeep_8(_Decimal128)
stress_funcdeep_9(_Decimal128)
stress_funccall_type(_Decimal128, (_Decimal128)stress_mwc64, cmp_ignore)
#endif

#if defined(HAVE_Float16) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Float16_put(const _Float16 a)
{
	g_put_val.float_val = (float)a;
}

stress_funccall_1(_Float16)
stress_funccall_2(_Float16)
stress_funccall_3(_Float16)
stress_funccall_4(_Float16)
stress_funccall_5(_Float16)
stress_funccall_6(_Float16)
stress_funccall_7(_Float16)
stress_funccall_8(_Float16)
stress_funccall_9(_Float16)
stress_funcdeep_1(_Float16)
stress_funcdeep_2(_Float16)
stress_funcdeep_3(_Float16)
stress_funcdeep_4(_Float16)
stress_funcdeep_5(_Float16)
stress_funcdeep_6(_Float16)
stress_funcdeep_7(_Float16)
stress_funcdeep_8(_Float16)
stress_funcdeep_9(_Float16)
stress_funccall_type(_Float16, (_Float16)stress_mwc32, cmp_fp)

#elif defined(HAVE_fp16) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void __fp16_put(const __fp16 a)
{
	g_put_val.float_val = (float)a;
}

stress_funccall_1(__fp16)
stress_funccall_2(__fp16)
stress_funccall_3(__fp16)
stress_funccall_4(__fp16)
stress_funccall_5(__fp16)
stress_funccall_6(__fp16)
stress_funccall_7(__fp16)
stress_funccall_8(__fp16)
stress_funccall_9(__fp16)
stress_funcdeep_1(__fp16)
stress_funcdeep_2(__fp16)
stress_funcdeep_3(__fp16)
stress_funcdeep_4(__fp16)
stress_funcdeep_5(__fp16)
stress_funcdeep_6(__fp16)
stress_funcdeep_7(__fp16)
stress_funcdeep_8(__fp16)
stress_funcdeep_9(__fp16)
stress_funccall_type(__fp16, (__fp16)stress_mwc32, cmp_fp)
#endif

#if defined(HAVE_Float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Float32_put(const _Float32 a)
{
	g_put_val.float_val = (float)a;
}

stress_funccall_1(_Float32)
stress_funccall_2(_Float32)
stress_funccall_3(_Float32)
stress_funccall_4(_Float32)
stress_funccall_5(_Float32)
stress_funccall_6(_Float32)
stress_funccall_7(_Float32)
stress_funccall_8(_Float32)
stress_funccall_9(_Float32)
stress_funcdeep_1(_Float32)
stress_funcdeep_2(_Float32)
stress_funcdeep_3(_Float32)
stress_funcdeep_4(_Float32)
stress_funcdeep_5(_Float32)
stress_funcdeep_6(_Float32)
stress_funcdeep_7(_Float32)
stress_funcdeep_8(_Float32)
stress_funcdeep_9(_Float32)
stress_funccall_type(_Float32, (_Float32)stress_mwc32, cmp_ignore)
#endif

#if defined(HAVE_Float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Float64_put(const _Float64 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(_Float64)
stress_funccall_2(_Float64)
stress_funccall_3(_Float64)
stress_funccall_4(_Float64)
stress_funccall_5(_Float64)
stress_funccall_6(_Float64)
stress_funccall_7(_Float64)
stress_funccall_8(_Float64)
stress_funccall_9(_Float64)
stress_funcdeep_1(_Float64)
stress_funcdeep_2(_Float64)
stress_funcdeep_3(_Float64)
stress_funcdeep_4(_Float64)
stress_funcdeep_5(_Float64)
stress_funcdeep_6(_Float64)
stress_funcdeep_7(_Float64)
stress_funcdeep_8(_Float64)
stress_funcdeep_9(_Float64)
stress_funccall_type(_Float64, (_Float64)stress_mwc64, cmp_ignore)
#endif


#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void __float80_put(const __float80 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(__float80)
stress_funccall_2(__float80)
stress_funccall_3(__float80)
stress_funccall_4(__float80)
stress_funccall_5(__float80)
stress_funccall_6(__float80)
stress_funccall_7(__float80)
stress_funccall_8(__float80)
stress_funccall_9(__float80)
stress_funcdeep_1(__float80)
stress_funcdeep_2(__float80)
stress_funcdeep_3(__float80)
stress_funcdeep_4(__float80)
stress_funcdeep_5(__float80)
stress_funcdeep_6(__float80)
stress_funcdeep_7(__float80)
stress_funcdeep_8(__float80)
stress_funcdeep_9(__float80)
stress_funccall_type(__float80, (__float80)stress_mwc64, cmp_fp)
#endif

#if defined(HAVE__float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void __float128_put(const __float128 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(__float128)
stress_funccall_2(__float128)
stress_funccall_3(__float128)
stress_funccall_4(__float128)
stress_funccall_5(__float128)
stress_funccall_6(__float128)
stress_funccall_7(__float128)
stress_funccall_8(__float128)
stress_funccall_9(__float128)
stress_funcdeep_1(__float128)
stress_funcdeep_2(__float128)
stress_funcdeep_3(__float128)
stress_funcdeep_4(__float128)
stress_funcdeep_5(__float128)
stress_funcdeep_6(__float128)
stress_funcdeep_7(__float128)
stress_funcdeep_8(__float128)
stress_funcdeep_9(__float128)
stress_funccall_type(__float128, (__float128)stress_mwc64, cmp_fp)
#elif defined(HAVE_Float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
static inline void _Float128_put(const _Float128 a)
{
	g_put_val.double_val = (double)a;
}

stress_funccall_1(_Float128)
stress_funccall_2(_Float128)
stress_funccall_3(_Float128)
stress_funccall_4(_Float128)
stress_funccall_5(_Float128)
stress_funccall_6(_Float128)
stress_funccall_7(_Float128)
stress_funccall_8(_Float128)
stress_funccall_9(_Float128)
stress_funcdeep_1(_Float128)
stress_funcdeep_2(_Float128)
stress_funcdeep_3(_Float128)
stress_funcdeep_4(_Float128)
stress_funcdeep_5(_Float128)
stress_funcdeep_6(_Float128)
stress_funcdeep_7(_Float128)
stress_funcdeep_8(_Float128)
stress_funcdeep_9(_Float128)
stress_funccall_type(_Float128, (_Float128)stress_mwc64, cmp_fp)
#endif

static bool stress_funccall_all(stress_args_t *args);

/*
 * Table of func call stress methods
 */
static const stress_funccall_method_info_t stress_funccall_methods[] = {
	{ "all",	stress_funccall_all },
	{ "bool",	stress_funccall_bool },
	{ "uint8",	stress_funccall_uint8_t },
	{ "uint16",	stress_funccall_uint16_t },
	{ "uint32",	stress_funccall_uint32_t },
	{ "uint64",	stress_funccall_uint64_t },
#if defined(HAVE_INT128_T)
	{ "uint128",	stress_funccall___uint128_t },
#endif
	{ "float",	stress_funccall_float },
#if defined(HAVE_Float16) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float16",	stress_funccall__Float16 },
#elif defined(HAVE_fp16) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float16",	stress_funccall___fp16 },
#endif
#if defined(HAVE_Float32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float32",	stress_funccall__Float32 },
#endif
#if defined(HAVE_Float64) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float64",	stress_funccall__Float64 },
#endif
#if defined(HAVE__float80) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float80",	stress_funccall___float80 },
#endif
#if defined(HAVE__float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",	stress_funccall___float128 },
#elif defined(HAVE_Float128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "float128",	stress_funccall__Float128 },
#endif
	{ "double",	stress_funccall_double },
	{ "longdouble",	stress_funccall_stress_long_double_t },
#if defined(HAVE_COMPLEX_H) &&			\
    defined(HAVE_COMPLEX) &&			\
    defined(__STDC_IEC_559_COMPLEX__) &&	\
    !defined(__UCLIBC__)
	{ "cfloat",	stress_funccall_stress_complex_float_t },
	{ "cdouble",	stress_funccall_stress_complex_double_t },
	{ "clongdouble",stress_funccall_stress_complex_long_double_t },
#endif
#if defined(HAVE_Decimal32) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal32",	stress_funccall__Decimal32 },
#endif
#if defined(HAVE_Decimal64) &&	\
     !defined(HAVE_COMPILER_CLANG)
	{ "decimal64",	stress_funccall__Decimal64 },
#endif
#if defined(HAVE_Decimal128) &&	\
    !defined(HAVE_COMPILER_CLANG)
	{ "decimal128",	stress_funccall__Decimal128 },
#endif
};

#define NUM_STRESS_FUNCCALL_METHODS	(SIZEOF_ARRAY(stress_funccall_methods))

static stress_metrics_t stress_funccall_metrics[NUM_STRESS_FUNCCALL_METHODS];

static bool stress_funccall_exercise(stress_args_t *args, const size_t method)
{
	bool success;
	double t;

	t = stress_time_now();
	success = stress_funccall_methods[method].func(args);
	stress_funccall_metrics[method].duration += stress_time_now() - t;
	stress_funccall_metrics[method].count += 1.0;

	if (!success && (method != 0)) {
		pr_fail("%s: verification failed with a nested %s function call return value\n",
			args->name, stress_funccall_methods[method].name);
	}
	return success;
}

static bool stress_funccall_all(stress_args_t *args)
{
	size_t i;
	bool success = true;

	for (i = 1; success && (i < NUM_STRESS_FUNCCALL_METHODS); i++) {
		success &= stress_funccall_exercise(args, i);
	}
	return success;
}

/*
 *  stress_funccall()
 *	stress various argument sized function calls
 */
static int stress_funccall(stress_args_t *args)
{
	size_t funccall_method = 0;
	bool success;

	size_t i, j;

	stress_zero_metrics(stress_funccall_metrics, NUM_STRESS_FUNCCALL_METHODS);

	(void)stress_get_setting("funccall-method", &funccall_method);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		success = stress_funccall_exercise(args, funccall_method);
	} while (success && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 1, j = 0; i < NUM_STRESS_FUNCCALL_METHODS; i++) {
		const double rate = (stress_funccall_metrics[i].duration > 0) ?
			stress_funccall_metrics[i].count / stress_funccall_metrics[i].duration : 0.0;

		if (rate > 0.0) {
			char msg[64];

			(void)snprintf(msg, sizeof(msg), "%s function invocations per sec",
					stress_funccall_methods[i].name);
			stress_metrics_set(args, j, msg, rate, STRESS_METRIC_HARMONIC_MEAN);
			j++;
		}
	}

	return success ? EXIT_SUCCESS : EXIT_FAILURE;
}

static const char *stress_funccall_method(const size_t i)
{
	return (i < NUM_STRESS_FUNCCALL_METHODS) ? stress_funccall_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_funccall_method, "funccall-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_funccall_method },
	END_OPT,
};

const stressor_info_t stress_funccall_info = {
	.stressor = stress_funccall,
	.classifier = CLASS_CPU,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
