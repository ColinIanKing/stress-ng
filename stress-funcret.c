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

typedef void (*stress_funcret_func)(const args_t *argse);

typedef struct {
	const char              *name;  /* human readable form of stressor */
	const stress_funcret_func   func;   /* the funcret method function */
} stress_funcret_method_info_t;

static const stress_funcret_method_info_t funcret_methods[];

static const help_t help[] = {
	{ NULL,	"funcret N",		"start N workers exercising function return copying" },
	{ NULL,	"funcret-ops N",	"stop after N function return bogo operations" },
	{ NULL,	"funcret-method M",	"select method of exercising a function return type" },
	{ NULL,	NULL,			NULL }
};

typedef long double 	long_double_t;

typedef struct {
	uint8_t		data[32];
} uint8x32_t;

typedef struct {
	uint8_t		data[128];
} uint8x128_t;

typedef struct {
	uint64_t	data[128];
} uint64x128_t;

#define stress_funcret1(type)					\
static type NOINLINE stress_funcret_ ## type ## 1(type a);	\
static type NOINLINE stress_funcret_ ## type ## 1(type a)	\
{								\
	type b;							\
								\
	(void)memcpy(&b, &a, sizeof(a));			\
	(void)memset(&a, 0, sizeof(a));				\
	return b;						\
}								\

#define stress_funcret_deep1(type)				\
static type NOINLINE stress_funcret_deep_ ## type ## 1(type a);	\
								\
static type NOINLINE stress_funcret_deep_ ## type ## 1(type a)	\
{								\
	type b;							\
								\
	(void)memcpy(&b, &a, sizeof(a));			\
	(void)memset(&a, 0, sizeof(a));				\
	return stress_funcret_ ## type ## 1(b);			\
}								\

#define stress_funcret_deeper1(type)				\
static type NOINLINE stress_funcret_deeper_ ## type ## 1(type a);\
								\
static type NOINLINE stress_funcret_deeper_ ## type ## 1(type a)\
{								\
	type b;							\
								\
	(void)memcpy(&b, &a, sizeof(a));			\
	(void)memset(&a, 0, sizeof(a));				\
								\
	return stress_funcret_deep_ ## type ## 1(		\
		stress_funcret_ ## type ## 1(b) );		\
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

stress_funcret1(long_double_t)
stress_funcret_deep1(long_double_t)
stress_funcret_deeper1(long_double_t)

#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
stress_funcret1(_Decimal32)
stress_funcret_deep1(_Decimal32)
stress_funcret_deeper1(_Decimal32)
#endif

#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
stress_funcret1(_Decimal64)
stress_funcret_deep1(_Decimal64)
stress_funcret_deeper1(_Decimal64)
#endif

#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
stress_funcret1(_Decimal128)
stress_funcret_deep1(_Decimal128)
stress_funcret_deeper1(_Decimal128)
#endif

#if defined(HAVE_FLOAT80) && !defined(__clang__)
stress_funcret1(__float80)
stress_funcret_deep1(__float80)
stress_funcret_deeper1(__float80)
#endif

#if defined(HAVE_FLOAT128) && !defined(__clang__)
stress_funcret1(__float128)
stress_funcret_deep1(__float128)
stress_funcret_deeper1(__float128)
#endif

stress_funcret1(uint8x32_t)
stress_funcret_deep1(uint8x32_t)
stress_funcret_deeper1(uint8x32_t)

stress_funcret1(uint8x128_t)
stress_funcret_deep1(uint8x128_t)
stress_funcret_deeper1(uint8x128_t)

stress_funcret1(uint64x128_t)
stress_funcret_deep1(uint64x128_t)
stress_funcret_deeper1(uint64x128_t)

#define stress_funcret_type(type)					\
static void NOINLINE stress_funcret_ ## type(const args_t *args);	\
									\
static void NOINLINE stress_funcret_ ## type(const args_t *args)	\
{									\
	register size_t ii;						\
	type a;								\
	uint8_t data[sizeof(a)];					\
									\
	for (ii = 0; ii < sizeof(data); ii++) 				\
		data[ii] = mwc8();					\
	(void)memcpy(&a, data, sizeof(a));				\
									\
	do {								\
		for (ii = 0; ii < 1000; ii++) {				\
			volatile type b;				\
			a = stress_funcret_ ## type ## 1(a);		\
			a = stress_funcret_deep_ ## type ## 1(a);	\
			a = stress_funcret_deeper_ ## type ## 1(a);	\
			b = a;						\
			(void)b;					\
		}							\
		inc_counter(args);					\
	} while (keep_stressing());					\
}

stress_funcret_type(uint8_t)
stress_funcret_type(uint16_t)
stress_funcret_type(uint32_t)
stress_funcret_type(uint64_t)
#if defined(HAVE_INT128_T)
stress_funcret_type(__uint128_t)
#endif
stress_funcret_type(float)
stress_funcret_type(double)
stress_funcret_type(long_double_t)
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
stress_funcret_type(_Decimal32)
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
stress_funcret_type(_Decimal64)
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
stress_funcret_type(_Decimal128)
#endif
#if defined(HAVE_FLOAT80) && !defined(__clang__)
stress_funcret_type(__float80)
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
stress_funcret_type(__float128)
#endif
stress_funcret_type(uint8x32_t)
stress_funcret_type(uint8x128_t)
stress_funcret_type(uint64x128_t)

/*
 * Table of func call stress methods
 */
static const stress_funcret_method_info_t funcret_methods[] = {
	{ "uint8",	stress_funcret_uint8_t },
	{ "uint16",	stress_funcret_uint16_t },
	{ "uint32",	stress_funcret_uint32_t },
	{ "uint64",	stress_funcret_uint64_t },
#if defined(HAVE_INT128_T)
	{ "uint128",	stress_funcret___uint128_t },
#endif
	{ "float",	stress_funcret_float },
	{ "double",	stress_funcret_double },
	{ "longdouble",	stress_funcret_long_double_t },
#if defined(HAVE_FLOAT80) && !defined(__clang__)
	{ "float80",	stress_funcret___float80 },
#endif
#if defined(HAVE_FLOAT128) && !defined(__clang__)
	{ "float128",	stress_funcret___float128 },
#endif
#if defined(HAVE_FLOAT_DECIMAL32) && !defined(__clang__)
	{ "decimal32",	stress_funcret__Decimal32 },
#endif
#if defined(HAVE_FLOAT_DECIMAL64) && !defined(__clang__)
	{ "decimal64",	stress_funcret__Decimal64 },
#endif
#if defined(HAVE_FLOAT_DECIMAL128) && !defined(__clang__)
	{ "decimal128",	stress_funcret__Decimal128 },
#endif
	{ "uint8x32",	stress_funcret_uint8x32_t },
	{ "uint8x128",	stress_funcret_uint8x128_t },
	{ "uint64x128",	stress_funcret_uint64x128_t },
	{ NULL,		NULL },
};

/*
 *  stress_set_funcret_method()
 *	set the default funccal stress method
 */
static int stress_set_funcret_method(const char *name)
{
	stress_funcret_method_info_t const *info;

	for (info = funcret_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			set_setting("funcret-method", TYPE_ID_UINTPTR_T, &info);
			return 0;
		}
	}

	(void)fprintf(stderr, "funcret-method must be one of:");
	for (info = funcret_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}

/*
 *  stress_funcret()
 *	stress various argument sized function calls
 */
static int stress_funcret(const args_t *args)
{
        const stress_funcret_method_info_t *funcret_method = &funcret_methods[3];

        (void)get_setting("funcret-method", &funcret_method);

        funcret_method->func(args);

	return EXIT_SUCCESS;
}

static void stress_funcret_set_default(void)
{
	stress_set_funcret_method("uint64");
}

static const opt_set_func_t opt_set_funcs[] = {
	{ OPT_funcret_method,	stress_set_funcret_method },
	{ 0,			NULL }
};

stressor_info_t stress_funcret_info = {
	.stressor = stress_funcret,
	.set_default = stress_funcret_set_default,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
