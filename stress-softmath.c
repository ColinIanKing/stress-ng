/*
 * Copyright (C)      2022 Colin Ian King.
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
#include "core-target-clones.h"

typedef int (*softmath_func_t)(const stress_args_t *args);

typedef struct {
	char *name;
	softmath_func_t func;
} softmath_method_t;

static const stress_help_t help[] = {
	{ NULL,	"softmath N",		"start N workers that exercise slow algorithms" },
	{ NULL, "softmath-method M",	"select softmath method M" },
	{ NULL,	"softmath-ops N",	"stop after N softmath bogo no-op operations" },
	{ NULL,	NULL,			NULL }
};

#define STRESS_SOFTMATH_MUL_OP(size, type)			\
static inline type OPTIMIZE3 stress_softmath_mul_op_ ## size(type x, type y)	\
{								\
	type r = 0;						\
								\
	while (x) {						\
		r += (x & 1) ? y : 0;				\
		x >>= 1;					\
		y <<= 1;					\
	}							\
	return r;						\
}

#define STRESS_SOFTMATH_MUL(size, type, rndfunc, fmt)		\
static inline int stress_softmath_mul_ ## size(			\
	const stress_args_t *args)				\
{								\
	int i;							\
	type x = rndfunc();					\
	type y = rndfunc();					\
								\
	for (i = 0; i < 10000; i++) {				\
		type r1, r2;					\
								\
		r1 = stress_softmath_mul_op_ ## size(x, y);	\
		r2 = x * y;					\
		if (r1 != r2) {					\
			pr_fail("%s: multiplication of unsigned"\
				" %d bit integers %" 		\
				fmt" x %" fmt " failed\n",	\
				args->name, size, x, y);	\
			return -1;				\
		}						\
		x += 127;					\
		y += 1123;					\
	}							\
	return 0;						\
}

STRESS_SOFTMATH_MUL_OP(64, uint64_t)
STRESS_SOFTMATH_MUL_OP(32, uint32_t)
STRESS_SOFTMATH_MUL_OP(16, uint16_t)
STRESS_SOFTMATH_MUL_OP(8, uint8_t)

STRESS_SOFTMATH_MUL(64, uint64_t, stress_mwc64, PRIu64)
STRESS_SOFTMATH_MUL(32, uint32_t, stress_mwc32, PRIu32)
STRESS_SOFTMATH_MUL(16, uint16_t, stress_mwc16, PRIu16)
STRESS_SOFTMATH_MUL(8, uint8_t, stress_mwc8, PRIu8)

#define STRESS_SOFTMATH_DIV_OP(size, type)			\
static inline type OPTIMIZE3 stress_softmath_div_op_ ## size(type x, type y)	\
{								\
	register uint64_t dividend, divisor;			\
	register uint64_t quotient, remainder;			\
	const int shift_bits = size - 1;			\
	const type top_bit = 1ULL << shift_bits;		\
	unsigned int i, bits;					\
								\
	dividend = x;						\
	divisor = y;						\
	bits = size;						\
	remainder = 0;						\
	quotient = 0;						\
								\
	if (y == 1)						\
		return x;					\
	if (x == y)						\
		return 1;					\
	if (x < y)						\
		return 0;					\
								\
	while (remainder < divisor) {				\
		register type bit = (dividend & top_bit) >> shift_bits;	\
		register type tmp = (remainder << 1) | bit;	\
								\
		if (tmp < divisor)				\
			break;					\
		remainder = tmp;				\
		dividend <<= 1;					\
		bits--;						\
	}							\
								\
	for (i = 0; i < bits; i++) {				\
		register type bit = (dividend & top_bit) >> shift_bits;	\
		register type t, q;				\
								\
		dividend <<= 1;					\
		remainder = (remainder << 1) | bit;		\
		t = remainder - divisor;			\
		q = !((t & top_bit) >> shift_bits);		\
		quotient = (quotient << 1) | q;			\
		remainder = q ? t : remainder;			\
	}							\
	return quotient;					\
}								

#define STRESS_SOFTMATH_DIV(size, type, rndfunc, fmt)		\
static inline int stress_softmath_div_ ## size(			\
	const stress_args_t *args)				\
{								\
	int i;							\
	type x = rndfunc();					\
	type y = rndfunc();					\
	type inv = ~(type)0;					\
	type mask = (((type)1U) << (type)(size - 1)) ^ inv;	\
	for (i = 0; i < 10000; i++) {				\
		type r1, r2;					\
								\
		y &= mask;					\
		if (y == 0)					\
			y++;					\
		r1 = stress_softmath_div_op_ ## size(x, y);	\
		r2 = x / y;					\
		if (r1 != r2) {					\
			pr_fail("%s: division of unsigned"	\
				" %d bit integers %" 		\
				fmt" / %" fmt " failed, %" fmt	\
				" vs %" fmt "\n",		\
				args->name, size, x, y, r1, r2);\
			return -1;				\
		}						\
		x += 1123;					\
		y += 127;					\
	}							\
	return 0;						\
}

STRESS_SOFTMATH_DIV_OP(64, uint64_t)
STRESS_SOFTMATH_DIV_OP(32, uint32_t)
STRESS_SOFTMATH_DIV_OP(16, uint16_t)
STRESS_SOFTMATH_DIV_OP(8, uint8_t)

STRESS_SOFTMATH_DIV(64, uint64_t, stress_mwc64, PRIu64)
STRESS_SOFTMATH_DIV(32, uint32_t, stress_mwc32, PRIu32)
STRESS_SOFTMATH_DIV(16, uint16_t, stress_mwc16, PRIu16)
STRESS_SOFTMATH_DIV(8, uint8_t, stress_mwc8, PRIu8)

#define STRESS_SOFTMATH_ISQRT_OP(size, type)			\
static inline type OPTIMIZE3 stress_softmath_isqrt_op_ ## size(type s)	\
{								\
	type x0 = s / 2;					\
								\
	if (x0 != 0) {						\
		type x1 = (x0 + s / x0) / 2;			\
								\
		while (x1 < x0) {				\
			x0 = x1;				\
			x1 = (x0 + s / x0) / 2;			\
		}						\
		return x0;					\
	} else {						\
		return s;					\
	}							\
}

#define STRESS_SOFTMATH_ISQRT(size, type, rndfunc, fmt)		\
static inline int stress_softmath_isqrt_ ## size(		\
	const stress_args_t *args)				\
{								\
	int i;							\
	type x = rndfunc();					\
	for (i = 0; i < 10000; i++) {				\
		type r1, r2;					\
								\
		r1 = stress_softmath_isqrt_op_ ## size(x);	\
		r2 = (type)truncl(sqrtl((long double)x));	\
		if (r1 != r2) {					\
			pr_fail("%s: sqrt of unsigned"		\
				" %d bit integer %" 		\
				fmt " failed, %" fmt		\
				" vs %" fmt "\n",		\
				args->name, size, x, r1, r2);	\
			return -1;				\
		}						\
		x += 1123;					\
	}							\
	return 0;						\
}

STRESS_SOFTMATH_ISQRT_OP(64, uint64_t)
STRESS_SOFTMATH_ISQRT_OP(32, uint32_t)
STRESS_SOFTMATH_ISQRT_OP(16, uint16_t)
STRESS_SOFTMATH_ISQRT_OP(8,  uint8_t)

STRESS_SOFTMATH_ISQRT(64, uint64_t, stress_mwc64, PRIu64)
STRESS_SOFTMATH_ISQRT(32, uint32_t, stress_mwc32, PRIu32)
STRESS_SOFTMATH_ISQRT(16, uint16_t, stress_mwc16, PRIu16)
STRESS_SOFTMATH_ISQRT(8,  uint8_t,  stress_mwc8,  PRIu8)

#define STRESS_SOFTMATH_IPOW_OP(size, type)			\
static inline type OPTIMIZE3 stress_softmath_ipow_op_ ## size(type base, type exp)	\
{								\
	register type result = 1;				\
	register type bit0 = (type)1U;				\
	for (;;) {						\
        	if (exp & bit0)					\
			result *= base;				\
        	exp >>= 1;					\
        	if (!exp)					\
            		break;					\
        	base *= base;					\
	}							\
	return result;						\
}

#define STRESS_SOFTMATH_IPOW(size, type, rndfunc, fmt)		\
static inline int stress_softmath_ipow_ ## size(		\
	const stress_args_t *args)				\
{								\
	int i;							\
								\
	for (i = 0; i < 10000; i++) {				\
		type x = (type)stress_mwc8();			\
		type y = (type)stress_mwc8() & 3;		\
		type r1, r2;					\
								\
		r1 = stress_softmath_ipow_op_ ## size(x, y);	\
		r2 = (type)truncl(powl((long double)x, (long double)y));\
		if (r1 != r2) {					\
			pr_fail("%s: pow of unsigned"		\
				" %d bit integer %" 		\
				fmt " failed, %" fmt		\
				" vs %" fmt "\n",		\
				args->name, size, x, r1, r2);	\
			return -1;				\
		}						\
	}							\
	return 0;						\
}

STRESS_SOFTMATH_IPOW_OP(64, uint64_t)
STRESS_SOFTMATH_IPOW_OP(32, uint32_t)
STRESS_SOFTMATH_IPOW_OP(16, uint16_t)
STRESS_SOFTMATH_IPOW_OP(8,  uint8_t)

STRESS_SOFTMATH_IPOW(64, uint64_t, stress_mwc64, PRIu64)
STRESS_SOFTMATH_IPOW(32, uint32_t, stress_mwc32, PRIu32)
STRESS_SOFTMATH_IPOW(16, uint16_t, stress_mwc16, PRIu16)
STRESS_SOFTMATH_IPOW(8,  uint8_t,  stress_mwc8,  PRIu8)

static const softmath_method_t softmath_methods[] = {
	{ "imul64",	stress_softmath_mul_64 },
	{ "imul32",	stress_softmath_mul_32 },
	{ "imul16",	stress_softmath_mul_16 },
	{ "imul8",	stress_softmath_mul_8 },
	{ "idiv64",	stress_softmath_div_64 },
	{ "idiv32",	stress_softmath_div_32 },
	{ "idiv16",	stress_softmath_div_16 },
	{ "idiv8",	stress_softmath_div_8 },
	{ "isqrt64",	stress_softmath_isqrt_64 },
	{ "isqrt32",	stress_softmath_isqrt_32 },
	{ "isqrt15",	stress_softmath_isqrt_16 },
	{ "isqrt8",	stress_softmath_isqrt_8 },
	{ "ipow64",	stress_softmath_ipow_64 },
	{ "ipow32",	stress_softmath_ipow_32 },
	{ "ipow16",	stress_softmath_ipow_16 },
	{ "ipow8",	stress_softmath_ipow_8 },
};

/*
 *  stress_softmath()
 *      stress less than optimal algorithms
 */
static int stress_softmath(const stress_args_t *args)
{
	int softmath_method = 0;

	(void)stress_get_setting("softmath-method", &softmath_method);

	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		size_t i;

		for (i = 0; i < SIZEOF_ARRAY(softmath_methods); i++) {
			softmath_methods[i].func(args);
		}
	} while (keep_stressing(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return EXIT_SUCCESS;
}

static const stress_opt_set_func_t opt_set_funcs[] = {
	//{ OPT_softmath_method,	stress_set_softmath_method },
	{ 0,                    NULL }
};

stressor_info_t stress_softmath_info = {
	.stressor = stress_softmath,
	.class = CLASS_CPU,
	.opt_set_funcs = opt_set_funcs,
	.help = help
};
