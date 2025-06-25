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
#include "core-put.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

static const stress_help_t help[] = {
	{ NULL,	"vecmath N",	 "start N workers performing vector math ops" },
	{ NULL,	"vecmath-ops N", "stop after N vector math bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_VECMATH)

typedef int8_t  stress_vint8_t  __attribute__ ((vector_size (16)));
typedef int16_t stress_vint16_t __attribute__ ((vector_size (16)));
typedef int32_t stress_vint32_t __attribute__ ((vector_size (16)));
typedef int64_t stress_vint64_t __attribute__ ((vector_size (16)));
#if defined(HAVE_INT128_T)
typedef __uint128_t stress_vint128_t __attribute__ ((vector_size (16)));
#endif

/*
 *  Convert various sized n * 8 bit tuples into n * 8 bit integers
 */
#define H8(a0)						\
	((int8_t)((uint8_t)a0))
#define H16(a0, a1)     				\
	((int16_t)(((uint16_t)a0 << 8) |		\
		   ((uint16_t)a1 << 0)))
#define H32(a0, a1, a2, a3)				\
	((int32_t)(((uint32_t)a0 << 24) |		\
		   ((uint32_t)a1 << 16) |		\
		   ((uint32_t)a2 <<  8) |		\
		   ((uint32_t)a3 <<  0)))
#define H64(a0, a1, a2, a3, a4, a5, a6, a7)		\
	((int64_t)(((uint64_t)a0 << 56) |		\
		   ((uint64_t)a1 << 48) |		\
		   ((uint64_t)a2 << 40) |		\
		   ((uint64_t)a3 << 32) |		\
		   ((uint64_t)a4 << 24) |		\
		   ((uint64_t)a5 << 16) |		\
		   ((uint64_t)a6 <<  8) | 		\
		   ((uint64_t)a7 <<  0)))

#if defined(HAVE_INT128_T)
#define H128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	((__int128_t)(((__int128_t)a0 << 120) |		\
		     ((__int128_t)a1 << 112) |		\
		     ((__int128_t)a2 << 104) |		\
		     ((__int128_t)a3 <<  96) |		\
		     ((__int128_t)a4 <<  88) |		\
		     ((__int128_t)a5 <<  80) |		\
		     ((__int128_t)a6 <<  72) |		\
		     ((__int128_t)a7 <<  64) |		\
		     ((__int128_t)a8 <<  56) |		\
		     ((__int128_t)a9 <<  48) |		\
		     ((__int128_t)aa <<  40) |		\
		     ((__int128_t)ab <<  32) |		\
		     ((__int128_t)ac <<  24) |		\
		     ((__int128_t)ad <<  16) |		\
		     ((__int128_t)ae <<   8) |		\
		     ((__int128_t)af <<   0)))		\

#endif

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
	H8(a0), H8(a1), H8(a2), H8(a3), H8(a4), H8(a5), H8(a6), H8(a7),		\
	H8(a8), H8(a9), H8(aa), H8(ab), H8(ac), H8(ad), H8(ae), H8(af)

#define INT8x16(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H16(a0, a1), H16(a2, a3), H16(a4, a5), H16(a6, a7),                     \
	H16(a8, a9), H16(aa, ab), H16(ac, ad), H16(ae, af)

#define INT4x32(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H32(a0, a1, a2, a3), H32(a4, a5, a6, a7),				\
	H32(a8, a9, aa, ab), H32(ac, ad, ae, af)

#define INT2x64(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)	\
	H64(a0, a1, a2, a3, a4, a5, a6, a7),					\
	H64(a8, a9, aa, ab, ac, ad, ae, af)

#if defined(HAVE_INT128_T)
#define INT1x128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)\
	H128(a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, aa, ab, ac, ad, ae, af)
#endif

/*
 *  Operations to run on each vector
 */
#define OPS(a, b, c, s, v23, v3) \
do {				\
	a += b;			\
	a |= b;			\
	a -= b;			\
	a &= ~b;		\
	a *= c;			\
	a = ~a;			\
	a *= s;			\
	a ^= c;			\
	a <<= 1;		\
	b >>= 1;		\
	b += c;			\
	a %= v23;		\
	c /= v3;		\
	b = b ^ c;		\
	c = b ^ c;		\
	b = b ^ c;		\
} while (0)

/*
 *  stress_vecmath()
 *	stress GCC vector maths
 */
#if defined(STRESS_ARCH_PPC64)
static int stress_vecmath(stress_args_t *args)
#else
static int TARGET_CLONES stress_vecmath(stress_args_t *args)
#endif
{
	int rc = EXIT_SUCCESS;
	/* checksum values */
	const uint8_t csum8_val =  (uint8_t)0x1b;
	const uint16_t csum16_val = (uint16_t)0xe76b;
	const uint32_t csum32_val = (uint32_t)0xd18aef8UL;
	const uint64_t csum64_val = (uint64_t)0x14eb06da7b6dd9c3ULL;
#if defined(HAVE_INT128_T)
	const uint64_t csum128lo_val = (uint64_t)0x10afc58fa61974ccULL;
	const uint64_t csum128hi_val = (uint64_t)0x0625922a4b5da4bbULL;
#endif

	const stress_vint8_t v23_8 = { V23(INT16x8) };
	const stress_vint8_t v3_8 = { V3(INT16x8) };
	const stress_vint16_t v23_16 = { V23(INT8x16) };
	const stress_vint16_t v3_16 = { V3(INT8x16) };
	const stress_vint32_t v23_32 = { V23(INT4x32) };
	const stress_vint32_t v3_32 = { V3(INT4x32) };
	const stress_vint64_t v23_64 = { V23(INT2x64) };
	const stress_vint64_t v3_64 = { V3(INT2x64) };
#if defined(HAVE_INT128_T)
	const stress_vint128_t v23_128 = { V23(INT1x128) };
	const stress_vint128_t v3_128 = { V3(INT1x128) };
#endif

	stress_catch_sigill();

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		int i;

		uint8_t csum8;
		stress_vint8_t a8 = { A(INT16x8) };
		stress_vint8_t b8 = { B(INT16x8) };
		stress_vint8_t c8 = { C(INT16x8) };
		stress_vint8_t s8 = { S(INT16x8) };

		uint16_t csum16;
		stress_vint16_t a16 = { A(INT8x16) };
		stress_vint16_t b16 = { B(INT8x16) };
		stress_vint16_t c16 = { C(INT8x16) };
		stress_vint16_t s16 = { S(INT8x16) };

		uint32_t csum32;
		stress_vint32_t a32 = { A(INT4x32) };
		stress_vint32_t b32 = { B(INT4x32) };
		stress_vint32_t c32 = { C(INT4x32) };
		stress_vint32_t s32 = { S(INT4x32) };

		uint64_t csum64;
		stress_vint64_t a64 = { A(INT2x64) };
		stress_vint64_t b64 = { B(INT2x64) };
		stress_vint64_t c64 = { C(INT2x64) };
		stress_vint64_t s64 = { S(INT2x64) };

#if defined(HAVE_INT128_T)
		uint64_t csum128lo, csum128hi;
		stress_vint128_t a128 = { A(INT1x128) };
		stress_vint128_t b128 = { B(INT1x128) };
		stress_vint128_t c128 = { C(INT1x128) };
		stress_vint128_t s128 = { S(INT1x128) };
#endif
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
		stress_bogo_inc(args);

		csum8 = a8[0]  ^ a8[1]  ^ a8[2]  ^ a8[3]  ^
			a8[4]  ^ a8[5]  ^ a8[6]  ^ a8[7]  ^
			a8[8]  ^ a8[9]  ^ a8[10] ^ a8[11] ^
			a8[12] ^ a8[13] ^ a8[14] ^ a8[15];
		stress_uint8_put(csum8);
		if (csum8 != csum8_val) {
			pr_fail("%s: 16 x 8 bit vector checksum mismatch, got 0x%2.2" PRIx8
				", expected 0x%" PRIx8 "\n", args->name, csum8, csum8_val);
			rc = EXIT_FAILURE;
		}

		csum16 = a16[0] ^ a16[1] ^ a16[2] ^ a16[3] ^
			 a16[4] ^ a16[5] ^ a16[6] ^ a16[7];
		stress_uint16_put(csum16);
		if (csum16 != csum16_val) {
			pr_fail("%s: 8 x 16 bit vector checksum mismatch, got 0x%4.4" PRIx16
				", expected 0x%" PRIx16 "\n", args->name, csum16, csum16_val);
			rc = EXIT_FAILURE;
		}

		csum32 = a32[0] ^ a32[1] ^ a32[2] ^ a32[3];
		stress_uint32_put(csum32);
		if (csum32 != csum32_val) {
			pr_fail("%s: 4 x 32 bit vector checksum mismatch, got 0x%8.8" PRIx32
				", expected 0x%" PRIx32 "\n", args->name, csum32, csum32_val);
			rc = EXIT_FAILURE;
		}

		csum64 = a64[0] ^ a64[1];
		stress_uint64_put(csum64);
		if (csum64 != csum64_val) {
			pr_fail("%s: 2 x 64 bit vector checksum mismatch, got 0x%16.16" PRIx64
				", expected 0x%" PRIx64 "\n", args->name, csum64, csum64_val);
			rc = EXIT_FAILURE;
		}

#if defined(HAVE_INT128_T)
		csum128lo = (uint64_t)(a128[0] & 0xffffffffffffffffULL);
		csum128hi = (uint64_t)(a128[0] >> 64);
		stress_uint128_put(a128[0]);
		if ((csum128lo != csum128lo_val) || (csum128hi != csum128hi_val)) {
			pr_fail("%s: 1 x 128 bit vector checksum mismatch, got 0x%16.16" PRIx64 ":%16.16" PRIx64
				", expected 0x%16.16" PRIx64 "%16.16" PRIx64 "\n", args->name,
				csum128hi, csum128lo, csum128hi_val, csum128lo_val);
			rc = EXIT_FAILURE;
		}
#endif
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_vecmath_info = {
	.stressor = stress_vecmath,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_vecmath_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#endif
