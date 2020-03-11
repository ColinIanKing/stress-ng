/*
 * Copyright (C) 2013-2020 Canonical, Ltd.
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

static const stress_help_t help[] = {
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
#if defined(STRESS_ARCH_PPC64) && \
    defined(__GNUC__) && \
    __GNUC__ < 6
#undef HAVE_VECMATH
#endif

#if defined(HAVE_VECMATH)

typedef int8_t  stress_vint8_t  __attribute__ ((vector_size (16)));
typedef int16_t stress_vint16_t __attribute__ ((vector_size (16)));
typedef int32_t stress_vint32_t __attribute__ ((vector_size (16)));
typedef int64_t stress_vint64_t __attribute__ ((vector_size (16)));
#if defined(HAVE_INT1x128_T)
typedef __uint128_t stress_vint128_t __attribute__ ((vector_size (16)));
#endif

/*
 *  Convert various sized n * 8 bit tuples into n * 8 bit integers
 */
#define H8(a0)						\
	((uint8_t)a0)
#define H16(a0, a1)     				\
	(((uint16_t)a0 << 8) |				\
	 ((uint16_t)a1))
#define H32(a0, a1, a2, a3)				\
	(((uint32_t)a0 << 24) | ((uint32_t)a1 << 16) |	\
	 ((uint32_t)a2 << 8)  | ((uint32_t)a3))
#define H64(a0, a1, a2, a3, a4, a5, a6, a7)		\
	(((uint64_t)H32(a0, a1, a2, a3) << 32) |	\
	 ((uint64_t)H32(a4, a5, a6, a7) << 0))
#define H128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	(((__uint128_t)H64(a0, a1, a2, a3, a4, a5, a6, a7) << 64) |	\
	 ((__uint128_t)H64(a8, a9, aa, ab, ac, ad, ae, af) << 0))

/*
 *  128 bit constants
 */
#define A(M)	M(0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	\
                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00)

#define B(M)	M(0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,	\
		  0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78)

#define C(M)	M(0x01, 0x02, 0x03, 0x02, 0x01, 0x02, 0x03, 0x02,	\
		  0x03, 0x02, 0x01, 0x02, 0x03, 0x02, 0x01, 0x02)

#define S(M)	M(0x01, 0x01, 0x01, 0x01, 0x02, 0x02, 0x02, 0x02,	\
		  0x01, 0x01, 0x02, 0x02, 0x01, 0x01, 0x02, 0x02)

#define	V23(M)	M(0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17,	\
		   0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17, 0x17)

#define V3(M)	M(0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,	\
		  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03)

/*
 *  Convert 16 x 8 bit values into various sized 128 bit vectors
 */
#define INT16x8(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af

#define INT8x16(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H16(a0, a1), H16(a2, a3), H16(a4, a5), H16(a6, a7),                     \
	H16(a8, a9), H16(aa, ab), H16(ac, ad), H16(ae, af)

#define INT4x32(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H32(a0, a1, a2, a3), H32(a4, a5, a6, a7),				\
	H32(a8, a9, aa, ab), H32(ac, ad, ae, af)

#define INT2x64(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H64(a0, a1, a2, a3, a4, a5, a6, a7),					\
	H64(a8, a9, aa, ab, ac, ad, ae, af)

#define INT1x128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)\
	H128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)

/*
 *  Operations to run on each vector
 */
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
#if defined(STRESS_ARCH_PPC64)
static int HOT stress_vecmath(const stress_args_t *args)
#else
static int HOT TARGET_CLONES stress_vecmath(const stress_args_t *args)
#endif
{
	stress_vint8_t a8 = { A(INT16x8) };
	stress_vint8_t b8 = { B(INT16x8) };
	stress_vint8_t c8 = { C(INT16x8) };
	stress_vint8_t s8 = { S(INT16x8) };
	const stress_vint8_t v23_8 = { V23(INT16x8) };
	const stress_vint8_t v3_8 = { V3(INT16x8) };

	stress_vint16_t a16 = { A(INT8x16) };
	stress_vint16_t b16 = { B(INT8x16) };
	stress_vint16_t c16 = { C(INT8x16) };
	stress_vint16_t s16 = { S(INT8x16) };
	const stress_vint16_t v23_16 = { V23(INT8x16) };
	const stress_vint16_t v3_16 = { V3(INT8x16) };

	stress_vint32_t a32 = { A(INT4x32) };
	stress_vint32_t b32 = { B(INT4x32) };
	stress_vint32_t c32 = { C(INT4x32) };
	stress_vint32_t s32 = { S(INT4x32) };
	const stress_vint32_t v23_32 = { V23(INT4x32) };
	const stress_vint32_t v3_32 = { V3(INT4x32) };

	stress_vint64_t a64 = { A(INT2x64) };
	stress_vint64_t b64 = { B(INT2x64) };
	stress_vint64_t c64 = { C(INT2x64) };
	stress_vint64_t s64 = { S(INT2x64) };
	const stress_vint64_t v23_64 = { V23(INT2x64) };
	const stress_vint64_t v3_64 = { V3(INT2x64) };

#if defined(HAVE_INT1x128_T)
	stress_vint128_t a128 = { A(INT1x128) };
	stress_vint128_t b128 = { B(INT1x128) };
	stress_vint128_t c128 = { C(INT1x128) };
	stress_vint128_t s128 = { S(INT1x128) };
	const stress_vint128_t v23_128 = { V23(INT1x128) };
	const stress_vint128_t v3_128 = { V3(INT1x128) };
#endif

	do {
		int i;
		for (i = 1000; i; i--) {
			/* Good mix of vector ops */
			OPS(a8, b8, c8, s8, v23_8, v3_8);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a64, b64, c64, s64, v23_64, v3_64);
#if defined(HAVE_INT1x128_T)
			OPS(a128, b128, c128, s128, v23_128, v3_128);
#endif

			OPS(a32, b32, c32, s32, v23_32, v3_32);
			OPS(a16, b16, c16, s16, v23_16, v3_16);
#if defined(HAVE_INT1x128_T)
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
#if defined(HAVE_INT1x128_T)
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
			OPS(a128, b128, c128, s128, v23_128, v3_128);
#endif
		}
		inc_counter(args);
	} while (keep_stressing());

	/* Forces the compiler to actually compute the terms */
	stress_uint8_put(a8[0]  ^ a8[1]  ^ a8[2]  ^ a8[3]  ^
		  a8[4]  ^ a8[5]  ^ a8[6]  ^ a8[7]  ^
		  a8[8]  ^ a8[9]  ^ a8[10] ^ a8[11] ^
		  a8[12] ^ a8[13] ^ a8[14] ^ a8[15]);
	stress_uint16_put(a16[0] ^ a16[1] ^ a16[2] ^ a16[3] ^
		   a16[4] ^ a16[5] ^ a16[6] ^ a16[7]);
	stress_uint32_put(a32[0] ^ a32[1] ^ a32[2] ^ a32[3]);
	stress_uint64_put(a64[0] ^ a64[1]);

#if defined(HAVE_INT1x128_T)
	stress_uint128_put(a128[0]);
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
