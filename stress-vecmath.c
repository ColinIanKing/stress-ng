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

static const help_t help[] = {
	{ NULL,	"vecmath N",	 "start N workers performing vector math ops" },
	{ NULL,	"vecmath-ops N", "stop after N vector math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

/*
 *  Clang 5.0 is the lowest version of clang that
 *  can build this without issues (clang 4.0 seems
 *  to spend forever optimizing this and causes the build
 *  to never complete)
 */
#if defined(__clang__) && \
    defined(__clang_major__) && \
    __clang_major__ < 5
#undef HAVE_VECMATH
#endif

/*
 *  gcc 5.x or earlier breaks on 128 bit vector maths on
 *  PPC64 for some reason with some flavours of the toolchain
 *  so disable this test for now
 */
#if defined(STRESS_PPC64) && \
    defined(__GNUC__) && \
    __GNUC__ < 6
#undef HAVE_VECMATH
#endif

#if defined(HAVE_VECMATH)

typedef int8_t  vint8_t  __attribute__ ((vector_size (16)));
typedef int16_t vint16_t __attribute__ ((vector_size (16)));
typedef int32_t vint32_t __attribute__ ((vector_size (16)));
typedef int64_t vint64_t __attribute__ ((vector_size (16)));
#if defined(HAVE_INT128_T)
typedef __uint128_t vint128_t __attribute__ ((vector_size (16)));
#endif

#define INT128(hi, lo)	(((__uint128_t)hi << 64) | (__uint128_t)lo)

#define OPS(a, b, c, s, v23, v3) \
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
	a %= v23;	\
	c /= v3;	\
	b = b ^ c;	\
	c = b ^ c;	\
	b = b ^ c;	\

/*
 *  stress_vecmath()
 *	stress GCC vector maths
 */
static int HOT TARGET_CLONES stress_vecmath(const args_t *args)
{
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
	const vint8_t v23_8 = {
		0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,
		0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17 };
	const vint8_t v3_8 = {
		0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
		0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };

	vint16_t a16 = {
		0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000 };
	vint16_t b16 = {
		0x0123, 0x4567, 0x89ab, 0xcdef, 0x0f1e, 0x2d3c, 0x4b5a, 0x6978 };
	vint16_t c16 = {
		0x0102, 0x0302, 0x0102, 0x0302, 0x0302, 0x0102, 0x0302, 0x0102 };
	vint16_t s16 = {
		0x0001, 0x0001, 0x0002, 0x0002, 0x0001, 0x0002, 0x0001, 0x0002 };
	const vint16_t v23_16 = {
		0x0017, 0x0017, 0x0017, 0x0017, 0x0017, 0x0017, 0x0017, 0x0017 };
	const vint16_t v3_16 = {
		0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003, 0x0003 };

	vint32_t a32 = {
		0x00000000, 0x00000000, 0x00000000, 0x00000000 };
	vint32_t b32 = {
		0x01234567, 0x89abcdef, 0x0f1e2d3c, 0x4b5a6978 };
	vint32_t c32 = {
		0x01020302, 0x01020302, 0x03020102, 0x03020102 };
	vint32_t s32 = {
		0x00000001, 0x00000002, 0x00000002, 0000000001 };
	const vint32_t v23_32 = {
		0x00000017, 0x00000017, 0x00000017, 0x00000017 };
	const vint32_t v3_32 = {
		0x00000003, 0x00000003, 0x00000003, 0x00000003 };

	vint64_t a64 = {
		0x0000000000000000ULL, 0x0000000000000000ULL };
	vint64_t b64 = {
		0x0123456789abcdefULL, 0x0f1e2d3c4b5a6979ULL };
	vint64_t c64 = {
		0x0102030201020302ULL, 0x0302010203020102ULL };
	vint64_t s64 = {
		0x0000000000000001ULL, 0x0000000000000002ULL };
	const vint64_t v23_64 = {
		0x0000000000000023ULL, 0x0000000000000023ULL };
	const vint64_t v3_64 = {
		0x0000000000000003ULL, 0x0000000000000003ULL };

#if defined(HAVE_INT128_T)
	vint128_t a128 = {
		INT128(0x0000000000000000ULL, 0x0000000000000000ULL) };
	vint128_t b128 = {
		INT128(0x0123456789abcdefULL, 0x0f1e2d3c4b5a6979ULL) };
	vint128_t c128 = {
		INT128(0x0102030201020302ULL, 0x0302010203020102ULL) };
	vint128_t s128 = {
		INT128(0x0000000000000001ULL, 0x0000000000000002ULL) };
	const vint128_t v23_128 = {
		INT128(0x0000000000000000ULL, 0x0000000000000023ULL) };
	const vint128_t v3_128 = {
		INT128(0x0000000000000000ULL, 0x0000000000000003ULL) };
#endif

	do {
		int i;
		for (i = 1000; i; i--) {
			/* Good mix of vector ops */
			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a64, b64, c64, s64, v23_64, v3_64);
#if defined(HAVE_INT128_T)
			OPS(a128, b128, c128, s128, v23_128, v3_128);
#endif

			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
#if defined(HAVE_INT128_T)
			OPS(a128, b128, c128, s128, v23_128, v3_128);
#endif
			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a64, b64, c64, s64, v23_64, v3_64);

			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a8, b8, c8, s8, v23_8, v3_8);

			OPS(a16, b16, c16, s16, v23_16, v3_16);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
			OPS(a16, b16, c16, s16, v23_16, v3_16);

			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a32, b32, c32, s32, v23_32, v3_32);

			OPS(a64, b64, c64, s64, v23_64, v3_64);
			OPS(a64, b64, c64, s64, v23_64, v3_64);
			OPS(a64, b64, c64, s64, v23_64, v3_64);
			OPS(a64, b64, c64, s64, v23_64, v3_64);
#if defined(HAVE_INT128_T)
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
#endif
		}
		inc_counter(args);
	} while (keep_stressing());

	/* Forces the compiler to actually compute the terms */
	uint64_put(a8[0] + a8[1] + a8[2] + a8[3] +
		   a8[4] + a8[5] + a8[6] + a8[7] +
		   a8[8] + a8[9] + a8[10] + a8[11] +
		   a8[12] + a8[13] + a8[14] + a8[15]);

	uint64_put(a16[0] + a16[1] + a16[2] + a16[3] +
		   a16[4] + a16[5] + a16[6] + a16[7]);

	uint64_put(a32[0] + a32[1] + a32[2] + a32[3]);

	uint64_put(a64[0] + a64[1]);

#if defined(HAVE_INT128_T)
	uint128_put(a128[0]);
#endif

	return EXIT_SUCCESS;
}

stressor_info_t stress_vecmath_info = {
	.stressor = stress_vecmath,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.help = help
};
#else
stressor_info_t stress_vecmath_info = {
	.stressor = stress_not_implemented,
	.class = CLASS_CPU | CLASS_CPU_CACHE,
	.help = help
};
#endif
