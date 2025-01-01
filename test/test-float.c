/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2025 Colin Ian King
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
#include <math.h>

#include "../core-version.h"

/* -O3 attribute support */
#if defined(__GNUC__) &&	\
    !defined(__clang__) &&	\
    NEED_GNUC(4,6,0)
#define OPTIMIZE3 	__attribute__((optimize("-O3")))
#else
#define OPTIMIZE3
#endif

/* From stress-cpu.c */
#define float_ops(_type, a, b, c, d, _sin, _cos)        \
	do {                                            \
		a = a + b;                              \
		b = a * c;                              \
		c = a - b;                              \
		d = a / b;                              \
		a = c / (_type)0.1923L;                 \
		b = c + a;                              \
		c = b * (_type)3.12L;                   \
		d = d + b + (_type)_sin(a);             \
		a = (b + c) / c;                        \
		b = b * c;                              \
		c = c + (_type)1.0L;                    \
		d = d - (_type)_sin(c);                 \
		a = a * (_type)_cos(b);                 \
		b = b + (_type)_cos(c);                 \
		c = (_type)_sin(a + b) / (_type)2.344L; \
		b = d - (_type)1.0L;                    \
	} while (0)

/* Avoid implicit int in the definition of test even if FLOAT is not known. */
typedef FLOAT float_type;

FLOAT a = 0.0, b = 0.0, c = 0.0, d = 0.0;
static float_type OPTIMIZE3 test(void)
{
	float_ops(FLOAT, a, b, c, d, sin, cos);
	float_ops(FLOAT, a, b, c, d, sinl, cosl);

	return a + b + c + d;
}

int main(void)
{
	return (int)test();
}
