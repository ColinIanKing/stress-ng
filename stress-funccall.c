/*
 * Copyright (C) 2013-2018 Canonical, Ltd.
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

#define uint8_t_put	uint8_put
#define uint16_t_put	uint16_put
#define uint32_t_put	uint32_put
#define uint64_t_put	uint64_put
#define __uint128_t_put	uint128_put
#define long_double_t_put long_double_put

typedef long double 	long_double_t;

#define stress_funccall_1(type)			\
void NOINLINE stress_funccall_ ## type ## _1(	\
	const type a);				\
						\
void NOINLINE stress_funccall_ ## type ## _1(	\
	const type a)				\
{						\
	type ## _put(a);			\
}						\

#define stress_funccall_2(type)			\
void NOINLINE stress_funccall_ ## type ## _2(	\
	const type a,				\
	const type b);				\
						\
void NOINLINE stress_funccall_ ## type ## _2(	\
	const type a,				\
	const type b)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
}						\

#define stress_funccall_3(type)			\
void NOINLINE stress_funccall_ ## type ## _3(	\
	const type a,				\
	const type b,				\
	const type c);				\
						\
void NOINLINE stress_funccall_ ## type ## _3(	\
	const type a,				\
	const type b,				\
	const type c)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
}						\

#define stress_funccall_4(type)			\
void NOINLINE stress_funccall_ ## type ## _4(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d);				\
						\
void NOINLINE stress_funccall_ ## type ## _4(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
}						\

#define stress_funccall_5(type)			\
void NOINLINE stress_funccall_ ## type ## _5(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e);				\
						\
void NOINLINE stress_funccall_ ## type ## _5(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
	type ## _put(e);			\
}						\

#define stress_funccall_6(type)			\
void NOINLINE stress_funccall_ ## type ## _6(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f);				\
						\
void NOINLINE stress_funccall_ ## type ## _6(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
	type ## _put(e);			\
	type ## _put(f);			\
}						\

#define stress_funccall_7(type)			\
void NOINLINE stress_funccall_ ## type ## _7(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g);				\
						\
void NOINLINE stress_funccall_ ## type ## _7(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
	type ## _put(e);			\
	type ## _put(f);			\
	type ## _put(g);			\
}						\

#define stress_funccall_8(type)			\
void NOINLINE stress_funccall_ ## type ## _8(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g,				\
	const type h);				\
						\
void NOINLINE stress_funccall_ ## type ## _8(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g,				\
	const type h)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
	type ## _put(e);			\
	type ## _put(f);			\
	type ## _put(g);			\
	type ## _put(h);			\
}						\

#define stress_funccall_9(type)			\
void NOINLINE stress_funccall_ ## type ## _9(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g,				\
	const type h,				\
	const type i);				\
						\
void NOINLINE stress_funccall_ ## type ## _9(	\
	const type a,				\
	const type b,				\
	const type c,				\
	const type d,				\
	const type e,				\
	const type f,				\
	const type g,				\
	const type h,				\
	const type i)				\
{						\
	type ## _put(a);			\
	type ## _put(b);			\
	type ## _put(c);			\
	type ## _put(d);			\
	type ## _put(e);			\
	type ## _put(f);			\
	type ## _put(g);			\
	type ## _put(h);			\
	type ## _put(i);			\
}						\

stress_funccall_1(uint8_t)
stress_funccall_2(uint8_t)
stress_funccall_3(uint8_t)
stress_funccall_4(uint8_t)
stress_funccall_5(uint8_t)
stress_funccall_6(uint8_t)
stress_funccall_7(uint8_t)
stress_funccall_8(uint8_t)
stress_funccall_9(uint8_t)

stress_funccall_1(uint16_t)
stress_funccall_2(uint16_t)
stress_funccall_3(uint16_t)
stress_funccall_4(uint16_t)
stress_funccall_5(uint16_t)
stress_funccall_6(uint16_t)
stress_funccall_7(uint16_t)
stress_funccall_8(uint16_t)
stress_funccall_9(uint16_t)

stress_funccall_1(uint32_t)
stress_funccall_2(uint32_t)
stress_funccall_3(uint32_t)
stress_funccall_4(uint32_t)
stress_funccall_5(uint32_t)
stress_funccall_6(uint32_t)
stress_funccall_7(uint32_t)
stress_funccall_8(uint32_t)
stress_funccall_9(uint32_t)

stress_funccall_1(uint64_t)
stress_funccall_2(uint64_t)
stress_funccall_3(uint64_t)
stress_funccall_4(uint64_t)
stress_funccall_5(uint64_t)
stress_funccall_6(uint64_t)
stress_funccall_7(uint64_t)
stress_funccall_8(uint64_t)
stress_funccall_9(uint64_t)

#if defined(STRESS_INT128)
stress_funccall_1(__uint128_t)
stress_funccall_2(__uint128_t)
stress_funccall_3(__uint128_t)
stress_funccall_4(__uint128_t)
stress_funccall_5(__uint128_t)
stress_funccall_6(__uint128_t)
stress_funccall_7(__uint128_t)
stress_funccall_8(__uint128_t)
stress_funccall_9(__uint128_t)
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

stress_funccall_1(double)
stress_funccall_2(double)
stress_funccall_3(double)
stress_funccall_4(double)
stress_funccall_5(double)
stress_funccall_6(double)
stress_funccall_7(double)
stress_funccall_8(double)
stress_funccall_9(double)

stress_funccall_1(long_double_t)
stress_funccall_2(long_double_t)
stress_funccall_3(long_double_t)
stress_funccall_4(long_double_t)
stress_funccall_5(long_double_t)
stress_funccall_6(long_double_t)
stress_funccall_7(long_double_t)
stress_funccall_8(long_double_t)
stress_funccall_9(long_double_t)

#define stress_funcall_type(type, rndfunc)			\
static void NOINLINE stress_funccall_ ## type(const args_t *args)\
{								\
	register int ii;					\
	type a, b, c, d, e, f, g, h, i;				\
								\
	a = rndfunc();						\
	b = rndfunc();						\
	c = rndfunc();						\
	d = rndfunc();						\
	e = rndfunc();						\
	f = rndfunc();						\
	g = rndfunc();						\
	h = rndfunc();						\
	i = rndfunc();						\
								\
	do {							\
		for (ii = 0; ii < 1000; ii++) {			\
			stress_funccall_ ## type ## _1(a);	\
			stress_funccall_ ## type ## _2(a, b);	\
			stress_funccall_ ## type ## _3(a, b,	\
				c);				\
			stress_funccall_ ## type ## _4(a, b, 	\
				c, d);				\
			stress_funccall_ ## type ## _5(a, b,	\
				c, d, e);			\
			stress_funccall_ ## type ## _6(a, b,	\
			 	c, d, e, f);			\
			stress_funccall_ ## type ## _7(a, b,	\
				c, d, e, f, g);			\
			stress_funccall_ ## type ## _8(a, b,	\
				c, d, e, f, g, h);		\
			stress_funccall_ ## type ## _9(a, b,	\
				c, d, e, f, g, h, i);		\
			}					\
		inc_counter(args);				\
	} while (keep_stressing());				\
}

stress_funcall_type(uint8_t, mwc8)
stress_funcall_type(uint16_t, mwc16)
stress_funcall_type(uint32_t, mwc32)
stress_funcall_type(uint64_t, mwc64)
#if defined(STRESS_INT128)
stress_funcall_type(__uint128_t, mwc64)
#endif
stress_funcall_type(float, (float)mwc64)
stress_funcall_type(double, (double)mwc64)
stress_funcall_type(long_double_t, (long double)mwc64)

/*
 * Table of func call stress methods
 */
static const stress_funccall_method_info_t funccall_methods[] = {
	{ "uint8",	stress_funccall_uint8_t },
	{ "uint16",	stress_funccall_uint16_t },
	{ "uint32",	stress_funccall_uint32_t },
	{ "uint64",	stress_funccall_uint64_t },
#if defined(STRESS_INT128)
	{ "uint128",	stress_funccall___uint128_t },
#endif
	{ "float",	stress_funccall_float },
	{ "double",	stress_funccall_double },
	{ "longdouble",	stress_funccall_long_double_t },
	{ NULL,		NULL },
};

/*
 *  stress_set_funccall_method()
 *	set the default funccal stress method
 */
int stress_set_funccall_method(const char *name)
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
int stress_funccall(const args_t *args)
{
        const stress_funccall_method_info_t *funccall_method = &funccall_methods[3];

        (void)get_setting("funccall-method", &funccall_method);

        funccall_method->func(args);

	return EXIT_SUCCESS;
}
