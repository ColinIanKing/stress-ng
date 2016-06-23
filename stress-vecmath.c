/*
 * Copyright (C) 2013-2016 Canonical, Ltd.
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
#define _GNU_SOURCE

#include "stress-ng.h"

#if defined(STRESS_VECMATH)

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

typedef int8_t  vint8_t  __attribute__ ((vector_size (16)));
typedef int16_t vint16_t __attribute__ ((vector_size (16)));
typedef int32_t vint32_t __attribute__ ((vector_size (16)));
typedef int64_t vint64_t __attribute__ ((vector_size (16)));

#define OPS(a, b, c, s)	\
	a += b;		\
	a |= b;		\
	a -= b;		\
	a &= ~b;	\
	a *= c;		\
	a = ~a;		\
	a *= s;		\
	a ^= c;		\
	a <<= 1;	\
	b >>= 1;	\
	b += c;		\
	a %= 23;	\
	c /= 3;		\
	b = b ^ c;	\
	c = b ^ c;	\
	b = b ^ c;	\

/*
 *  stress_vecmath()
 *	stress GCC vector maths
 */
int HOT OPTIMIZE3 stress_vecmath(
	uint64_t *const counter,
	const uint32_t instance,
	const uint64_t max_ops,
	const char *name)
{
	(void)instance;
	(void)name;

	vint8_t a8 = {
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	vint8_t b8 = {
		0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
		0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78 };
	vint8_t c8 = {
		0x01, 0x02, 0x03, 0x02, 0x01, 0x02, 0x03, 0x02,
		0x03, 0x02, 0x01, 0x02, 0x03, 0x02, 0x01, 0x02 };
	vint8_t s8 = {
		0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02,
		0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x02, 0x02 };

	vint16_t a16 = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
	vint16_t b16 = {
		0x0123, 0x4567, 0x89ab, 0xcdef, 0x0f1e, 0x2d3c, 0x4b5a, 0x6978 };
	vint16_t c16 = {
		0x0102, 0x0302, 0x0102, 0x0302, 0x0302, 0x0102, 0x0302, 0x0102 };
	vint16_t s16 = {
		0x0001, 0x0001, 0x0002, 0x0002, 0x0001, 0x0002, 0x0001, 0x0002 };

	vint32_t a32 = {
		0x00000000, 0x00000000, 0x00000000, 0x00000000 };
	vint32_t b32 = {
		0x01234567, 0x89abcdef, 0x0f1e2d3c, 0x4b5a6978 };
	vint32_t c32 = {
		0x01020302, 0x01020302, 0x03020102, 0x03020102 };
	vint32_t s32 = {
		0x00000001, 0x00000002, 0x00000002, 0000000001 };

	vint64_t a64 = {
		0x0000000000000000ULL, 0x0000000000000000ULL };
	vint64_t b64 = {
		0x0123456789abcdefULL, 0x0f1e2d3c4b5a6979ULL };
	vint64_t c64 = {
		0x0102030201020302ULL, 0x0302010203020102ULL };
	vint64_t s64 = {
		0x0000000000000001ULL, 0x0000000000000002ULL };

	do {
		int i;
		for (i = 1000; i; i--) {
			/* Good mix of vector ops */
			OPS(a8, b8, c8, s8);
			OPS(a16, b16, c16, s16);
			OPS(a32, b32, c32, s32);
			OPS(a64, b64, c64, s64);

			OPS(a32, b32, c32, s32);
			OPS(a16, b16, c16, s16);
			OPS(a8, b8, c8, s8);
			OPS(a64, b64, c64, s64);

			OPS(a8, b8, c8, s8);
			OPS(a8, b8, c8, s8);
			OPS(a8, b8, c8, s8);
			OPS(a8, b8, c8, s8);

			OPS(a16, b16, c16, s16);
			OPS(a16, b16, c16, s16);
			OPS(a16, b16, c16, s16);
			OPS(a16, b16, c16, s16);

			OPS(a32, b32, c32, s32);
			OPS(a32, b32, c32, s32);
			OPS(a32, b32, c32, s32);
			OPS(a32, b32, c32, s32);

			OPS(a64, b64, c64, s64);
			OPS(a64, b64, c64, s64);
			OPS(a64, b64, c64, s64);
			OPS(a64, b64, c64, s64);
		}
		(*counter)++;
	} while (opt_do_run && (!max_ops || *counter < max_ops));

	/* Forces the compiler to actually compute the terms */
	uint64_put(a8[0] + a8[1] + a8[2] + a8[3] +
		   a8[4] + a8[5] + a8[6] + a8[7] +
		   a8[8] + a8[9] + a8[10] + a8[11] +
		   a8[12] + a8[13] + a8[14] + a8[15]);

	uint64_put(a16[0] + a16[1] + a16[2] + a16[3] +
		   a16[4] + a16[5] + a16[6] + a16[7]);

	uint64_put(a32[0] + a32[1] + a32[2] + a32[3]);

	uint64_put(a64[0] + a64[1]);

	return EXIT_SUCCESS;
}

#endif
