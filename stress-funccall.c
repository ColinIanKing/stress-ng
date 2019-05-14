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

typedef void (*stress_funccall_func)(const args_t *argse);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_funccall_func   func;   /* the funccall method function */
} stress_funccall_method_info_t;

static const stress_funccall_method_info_t funccall_methods[];

static const help_t help[] = {
	{ NULL,	"funccall N",		"start N workers exercising 1 to 9 arg functions" },
	{ NULL,	"funccall-ops N",	"stop after N function call bogo operations" },
	{ NULL,	"funccall-method M",	"select function call method M" },
	{ NULL,	NULL,			NULL }
};

#define uint8_t_put	uint8_put
#define uint16_t_put	uint16_put
#define uint32_t_put	uint32_put
#define uint64_t_put	uint64_put
#define __uint128_t_put	uint128_put
#define long_double_t_put long_double_put

typedef long double 	long_double_t;

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

stress_funccall_1(uint8_t)
stress_funccall_2(uint8_t)
stress_funccall_3(uint8_t)
stress_funccall_4(uint8_t)
stress_funccall_5(uint8_t)
stress_funccall_6(uint8_t)
stress_funccall_7(uint8_t)
stress_funccall_8(uint8_t)
stress_funccall_9(uint8_t)

stress_funcdeep_2(uint8_t)
stress_funcdeep_3(uint8_t)
stress_funcdeep_4(uint8_t)
stress_funcdeep_5(uint8_t)
stress_funcdeep_6(uint8_t)
stress_funcdeep_7(uint8_t)
stress_funcdeep_8(uint8_t)
stress_funcdeep_9(uint8_t)

stress_funccall_1(uint16_t)
stress_funccall_2(uint16_t)
stress_funccall_3(uint16_t)
stress_funccall_4(uint16_t)
stress_funccall_5(uint16_t)
stress_funccall_6(uint16_t)
stress_funccall_7(uint16_t)
stress_funccall_8(uint16_t)
stress_funccall_9(uint16_t)

stress_funcdeep_2(uint16_t)
stress_funcdeep_3(uint16_t)
stress_funcdeep_4(uint16_t)
stress_funcdeep_5(uint16_t)
stress_funcdeep_6(uint16_t)
stress_funcdeep_7(uint16_t)
stress_funcdeep_8(uint16_t)
stress_funcdeep_9(uint16_t)

stress_funccall_1(uint32_t)
stress_funccall_2(uint32_t)
stress_funccall_3(uint32_t)
stress_funccall_4(uint32_t)
stress_funccall_5(uint32_t)
stress_funccall_6(uint32_t)
stress_funccall_7(uint32_t)
stress_funccall_8(uint32_t)
stress_funccall_9(uint32_t)

stress_funcdeep_2(uint32_t)
stress_funcdeep_3(uint32_t)
stress_funcdeep_4(uint32_t)
stress_funcdeep_5(uint32_t)
stress_funcdeep_6(uint32_t)
stress_funcdeep_7(uint32_t)
stress_funcdeep_8(uint32_t)
stress_funcdeep_9(uint32_t)

stress_funccall_1(uint64_t)
stress_funccall_2(uint64_t)
stress_funccall_3(uint64_t)
stress_funccall_4(uint64_t)
stress_funccall_5(uint64_t)
stress_funccall_6(uint64_t)
stress_funccall_7(uint64_t)
stress_funccall_8(uint64_t)
stress_funccall_9(uint64_t)

stress_funcdeep_2(uint64_t)
stress_funcdeep_3(uint64_t)
stress_funcdeep_4(uint64_t)
stress_funcdeep_5(uint64_t)
stress_funcdeep_6(uint64_t)
stress_funcdeep_7(uint64_t)
stress_funcdeep_8(uint64_t)
stress_funcdeep_9(uint64_t)

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

stress_funcdeep_2(__uint128_t)
stress_funcdeep_3(__uint128_t)
stress_funcdeep_4(__uint128_t)
stress_funcdeep_5(__uint128_t)
stress_funcdeep_6(__uint128_t)
stress_funcdeep_7(__uint128_t)
stress_funcdeep_8(__uint128_t)
stress_funcdeep_9(__uint128_t)
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

stress_funcdeep_2(float)
stress_funcdeep_3(float)
stress_funcdeep_4(float)
stress_funcdeep_5(float)
stress_funcdeep_6(float)
stress_funcdeep_7(float)
stress_funcdeep_8(float)
stress_funcdeep_9(float)

stress_funccall_1(double)
stress_funccall_2(double)
stress_funccall_3(double)
stress_funccall_4(double)
stress_funccall_5(double)
stress_funccall_6(double)
stress_funccall_7(double)
stress_funccall_8(double)
stress_funccall_9(double)

stress_funcdeep_2(double)
stress_funcdeep_3(double)
stress_funcdeep_4(double)
stress_funcdeep_5(double)
stress_funcdeep_6(double)
stress_funcdeep_7(double)
stress_funcdeep_8(double)
stress_funcdeep_9(double)

stress_funccall_1(long_double_t)
stress_funccall_2(long_double_t)
stress_funccall_3(long_double_t)
stress_funccall_4(long_double_t)
stress_funccall_5(long_double_t)
stress_funccall_6(long_double_t)
stress_funccall_7(long_double_t)
stress_funccall_8(long_double_t)
stress_funccall_9(long_double_t)

stress_funcdeep_2(long_double_t)
stress_funcdeep_3(long_double_t)
stress_funcdeep_4(long_double_t)
stress_funcdeep_5(long_double_t)
stress_funcdeep_6(long_double_t)
stress_funcdeep_7(long_double_t)
stress_funcdeep_8(long_double_t)
stress_funcdeep_9(long_double_t)

#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
static inline void ALWAYS_INLINE _Decimal32_put(const _Decimal32 a)
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

stress_funcdeep_2(_Decimal32)
stress_funcdeep_3(_Decimal32)
stress_funcdeep_4(_Decimal32)
stress_funcdeep_5(_Decimal32)
stress_funcdeep_6(_Decimal32)
stress_funcdeep_7(_Decimal32)
stress_funcdeep_8(_Decimal32)
stress_funcdeep_9(_Decimal32)
#endif

#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
static inline void ALWAYS_INLINE _Decimal64_put(const _Decimal64 a)
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

stress_funcdeep_2(_Decimal64)
stress_funcdeep_3(_Decimal64)
stress_funcdeep_4(_Decimal64)
stress_funcdeep_5(_Decimal64)
stress_funcdeep_6(_Decimal64)
stress_funcdeep_7(_Decimal64)
stress_funcdeep_8(_Decimal64)
stress_funcdeep_9(_Decimal64)
#endif

#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
static inline void ALWAYS_INLINE _Decimal128_put(const _Decimal128 a)
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

stress_funcdeep_2(_Decimal128)
stress_funcdeep_3(_Decimal128)
stress_funcdeep_4(_Decimal128)
stress_funcdeep_5(_Decimal128)
stress_funcdeep_6(_Decimal128)
stress_funcdeep_7(_Decimal128)
stress_funcdeep_8(_Decimal128)
stress_funcdeep_9(_Decimal128)
#endif

#if defined(HAVE_FLOAT80) && !defined(__clang__)
static inline void ALWAYS_INLINE __float80_put(const __float80 a)
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

stress_funcdeep_2(__float80)
stress_funcdeep_3(__float80)
stress_funcdeep_4(__float80)
stress_funcdeep_5(__float80)
stress_funcdeep_6(__float80)
stress_funcdeep_7(__float80)
stress_funcdeep_8(__float80)
stress_funcdeep_9(__float80)
#endif

#if defined(HAVE_FLOAT128) && !defined(__clang__)
static inline void ALWAYS_INLINE __float128_put(const __float128 a)
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

stress_funcdeep_2(__float128)
stress_funcdeep_3(__float128)
stress_funcdeep_4(__float128)
stress_funcdeep_5(__float128)
stress_funcdeep_6(__float128)
stress_funcdeep_7(__float128)
stress_funcdeep_8(__float128)
stress_funcdeep_9(__float128)
#endif

#define stress_funccall_type(type, rndfunc)				\
static void NOINLINE stress_funccall_ ## type(const args_t *args);	\
									\
static void NOINLINE stress_funccall_ ## type(const args_t *args)	\
{									\
	register int ii;						\
	type a, b, c, d, e, f, g, h, i;					\
									\
	a = rndfunc();							\
	b = rndfunc();							\
	c = rndfunc();							\
	d = rndfunc();							\
	e = rndfunc();							\
	f = rndfunc();							\
	g = rndfunc();							\
	h = rndfunc();							\
	i = rndfunc();							\
									\
	do {								\
		for (ii = 0; ii < 1000; ii++) {				\
			type res = 					\
			(stress_funccall_ ## type ## _1(a) + 		\
			 stress_funccall_ ## type ## _2(a, b) +		\
			 stress_funccall_ ## type ## _3(a, b,		\
				c) +					\
			 stress_funccall_ ## type ## _4(a, b, 		\
				c, d) + 				\
			 stress_funccall_ ## type ## _5(a, b,		\
				c, d, e) +				\
			 stress_funccall_ ## type ## _6(a, b,		\
				c, d, e, f) + 				\
			 stress_funccall_ ## type ## _7(a, b,		\
				c, d, e, f, g) + 			\
			 stress_funccall_ ## type ## _8(a, b,		\
				c, d, e, f, g, h) + 			\
			 stress_funccall_ ## type ## _9(a, b,		\
				c, d, e, f, g, h, i));			\
									\
			res += 						\
			(stress_funcdeep_ ## type ## _2(a, b) +		\
			 stress_funcdeep_ ## type ## _3(a, b,		\
				c) + 					\
			 stress_funcdeep_ ## type ## _4(a, b, 		\
				c, d) +					\
			 stress_funcdeep_ ## type ## _5(a, b,		\
				c, d, e) +				\
			 stress_funcdeep_ ## type ## _6(a, b,		\
				c, d, e, f) +				\
			 stress_funcdeep_ ## type ## _7(a, b,		\
				c, d, e, f, g) +			\
			 stress_funcdeep_ ## type ## _8(a, b,		\
				c, d, e, f, g, h) +			\
			 stress_funcdeep_ ## type ## _9(a, b,		\
				c, d, e, f, g, h, i));			\
			type ## _put(res);				\
			}						\
		inc_counter(args);					\
	} while (keep_stressing());					\
}

stress_funccall_type(uint8_t, mwc8)
stress_funccall_type(uint16_t, mwc16)
stress_funccall_type(uint32_t, mwc32)
stress_funccall_type(uint64_t, mwc64)
#if defined(HAVE_INT128_T)
stress_funccall_type(__uint128_t, mwc64)
#endif
stress_funccall_type(float, (float)mwc64)
stress_funccall_type(double, (double)mwc64)
stress_funccall_type(long_double_t, (long double)mwc64)
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
stress_funccall_type(_Decimal32, (_Decimal32)mwc64)
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
stress_funccall_type(_Decimal64, (_Decimal64)mwc64)
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
stress_funccall_type(_Decimal128, (_Decimal128)mwc64)
#endif
#if defined(HAVE_FLOAT80) && !defined(__clang__)
stress_funccall_type(__float80, (__float80)mwc64)
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
stress_funccall_type(__float128, (__float128)mwc64)
#endif

/*
 * Table of func call stress methods
 */
static const stress_funccall_method_info_t funccall_methods[] = {
	{ "uint8",	stress_funccall_uint8_t },
	{ "uint16",	stress_funccall_uint16_t },
	{ "uint32",	stress_funccall_uint32_t },
	{ "uint64",	stress_funccall_uint64_t },
#if defined(HAVE_INT128_T)
	{ "uint128",	stress_funccall___uint128_t },
#endif
	{ "float",	stress_funccall_float },
	{ "double",	stress_funccall_double },
	{ "longdouble",	stress_funccall_long_double_t },
#if defined(HAVE_FLOAT80) && !defined(__clang__)
	{ "float80",	stress_funccall___float80 },
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
	{ "float128",	stress_funccall___float128 },
#endif
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
	{ "decimal32",	stress_funccall__Decimal32 },
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
	{ "decimal64",	stress_funccall__Decimal64 },
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
	{ "decimal128",	stress_funccall__Decimal128 },
#endif
	{ NULL,		NULL },
};

/*
 *  stress_set_funccall_method()
 *	set the default funccal stress method
 */
static int stress_set_funccall_method(const char *name)
{
	stress_funccall_method_info_t const *info;

	for (info = funccall_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("funccall-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "funccall-method must be one of:");
	for (info = funccall_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_funccall()
 *	stress various argument sized function calls
 */
static int stress_funccall(const args_t *args)
{
        const stress_funccall_method_info_t *funccall_method = &funccall_methods[3];

        (void)get_setting("funccall-method", &funccall_method);

        funccall_method->func(args);

	return EXIT_SUCCESS;
}


static void stress_funccall_set_default(void)
{
	stress_set_funccall_method("uint64");
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_funccall_method,	stress_set_funccall_method },
	{ 0,			NULL }
};

stressor_info_t stress_funccall_info = {
	.stressor = stress_funccall,
	.set_default = stress_funccall_set_default,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
