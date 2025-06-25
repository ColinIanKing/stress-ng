/*
 * Copyright (C) 2024-2025 Colin Ian King.
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
#include "core-pragma.h"
#include "core-target-clones.h"
#include "core-vecmath.h"

static const stress_help_t help[] = {
	{ NULL,	"veccmp N",	 "start N workers performing integer vector comparison ops" },
	{ NULL,	"veccmp-ops N", "stop after N integer vector comparison bogo operations" },
	{ NULL,	NULL,		 NULL }
};

#if defined(HAVE_VECMATH) && \
    ((defined(HAVE_COMPILER_GCC_OR_MUSL) && NEED_GNUC(4,8,4)) ||	\
     (defined(HAVE_COMPILER_CLANG) && NEED_CLANG(14,0,0)) ||		\
     (defined(HAVE_COMPILER_ICX) && NEED_ICX(2025,0,0)) || 		\
     (defined(HAVE_COMPILER_ICC) && NEED_ICC(2021,5,0)))

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
#define A(M)	M(0x7f, 0x8e, 0x9d, 0xac, 0xbb, 0xca, 0xd9, 0xe8,	\
		  0xf7, 0x06, 0x15, 0x24, 0x33, 0x42, 0x51, 0x60)

#define B(M)	M(0xff, 0xee, 0xdd, 0xcc, 0xbb, 0xaa, 0x99, 0x88,	\
		  0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, 0x00)

#define C(M)	M(0x0c, 0x8d, 0x1e, 0x9f, 0x20, 0xa1, 0x32, 0xb3,	\
		  0x44, 0xc5, 0x56, 0xd7, 0x68, 0xe9, 0x7a, 0xfb)

#define D(M)	M(0x02, 0x03, 0x07, 0x0b, 0x0d, 0x11, 0x13, 0x17,	\
		  0x1d, 0x1f, 0x25, 0x29, 0x2b, 0x2f, 0x35, 0x3b)

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
#define OPS(a, b, c, d) 	\
do {				\
	a += (b > c);		\
	c += d;			\
	a ^= (b < c);		\
	b -= d;			\
	a += (b == c);		\
	c += d;			\
	a ^= (b != c);		\
	b -= d;			\
	a += (b >= c);		\
	c += d;			\
	a ^= (b <= c);		\
	b -= d;			\
} while (0)

/*
 *  stress_veccmp()
 *	stress GCC vector maths
 */
#if defined(STRESS_ARCH_PPC64)
static int stress_veccmp(stress_args_t *args)
#else
static int TARGET_CLONES stress_veccmp(stress_args_t *args)
#endif
{
	int rc = EXIT_SUCCESS;
	/* checksum values */
	const uint8_t csum8_val =  (uint8_t)0x93;
	const uint16_t csum16_val = (uint16_t)0x0099;
	const uint32_t csum32_val = (uint32_t)0x000001bdUL;
	const uint64_t csum64_val = (uint64_t)0x88888888888883a5ULL;
#if defined(HAVE_INT128_T)
	const uint64_t csum128lo_val = (uint64_t)0xf70615243342545cULL;
	const uint64_t csum128hi_val = (uint64_t)0x7f8e9dacbbcad9e8ULL;
#endif

	stress_catch_sigill();

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		register int i;

		uint8_t csum8;
		stress_vint8_t a8 = { A(INT16x8) };
		stress_vint8_t b8 = { B(INT16x8) };
		stress_vint8_t c8 = { C(INT16x8) };
		stress_vint8_t d8 = { D(INT16x8) };

		uint16_t csum16;
		stress_vint16_t a16 = { A(INT8x16) };
		stress_vint16_t b16 = { B(INT8x16) };
		stress_vint16_t c16 = { C(INT8x16) };
		stress_vint16_t d16 = { D(INT8x16) };

		uint32_t csum32;
		stress_vint32_t a32 = { A(INT4x32) };
		stress_vint32_t b32 = { B(INT4x32) };
		stress_vint32_t c32 = { C(INT4x32) };
		stress_vint32_t d32 = { D(INT4x32) };

		uint64_t csum64;
		stress_vint64_t a64 = { A(INT2x64) };
		stress_vint64_t b64 = { B(INT2x64) };
		stress_vint64_t c64 = { C(INT2x64) };
		stress_vint64_t d64 = { D(INT2x64) };

#if defined(HAVE_INT128_T)
		uint64_t csum128lo, csum128hi;
		stress_vint128_t a128 = { A(INT1x128) };
		stress_vint128_t b128 = { B(INT1x128) };
		stress_vint128_t c128 = { C(INT1x128) };
		stress_vint128_t d128 = { D(INT1x128) };
#endif
PRAGMA_UNROLL_N(8)
		for (i = 1000; i; i--) {
			/* Good mix of vector ops */
			OPS(a8, b8, c8, d8);
			OPS(a8, c8, d8, b8);
			OPS(a8, d8, b8, c8);
			OPS(a16, b16, c16, d16);
			OPS(a16, c16, d16, b16);
			OPS(a16, d16, b16, c16);
			OPS(a32, b32, c32, d32);
			OPS(a32, c32, d32, b32);
			OPS(a32, d32, b32, c32);
			OPS(a64, b64, c64, d64);
			OPS(a64, c64, d64, b64);
			OPS(a64, d64, b64, c64);
#if defined(HAVE_INT128_T)
			OPS(a128, b128, c128, d128);
			OPS(a128, c128, d128, b128);
			OPS(a128, d128, b128, c128);
#endif
		}
		csum8 = a8[0]  ^ a8[1]  ^ a8[2]  ^ a8[3]  ^
			a8[4]  ^ a8[5]  ^ a8[6]  ^ a8[7]  ^
			a8[8]  ^ a8[9]  ^ a8[10] ^ a8[11] ^
			a8[12] ^ a8[13] ^ a8[14] ^ a8[15];
		stress_uint8_put(csum8);
		if (UNLIKELY(csum8 != csum8_val)) {
			pr_fail("%s: 16 x 8 bit vector checksum mismatch, got 0x%2.2" PRIx8
				", expected 0x%" PRIx8 "\n", args->name, csum8, csum8_val);
			rc = EXIT_FAILURE;
			break;
		}

		csum16 = a16[0] ^ a16[1] ^ a16[2] ^ a16[3] ^
			 a16[4] ^ a16[5] ^ a16[6] ^ a16[7];
		stress_uint16_put(csum16);
		if (UNLIKELY(csum16 != csum16_val)) {
			pr_fail("%s: 8 x 16 bit vector checksum mismatch, got 0x%4.4" PRIx16
				", expected 0x%" PRIx16 "\n", args->name, csum16, csum16_val);
			rc = EXIT_FAILURE;
			break;
		}

		csum32 = a32[0] ^ a32[1] ^ a32[2] ^ a32[3];
		stress_uint32_put(csum32);
		if (UNLIKELY(csum32 != csum32_val)) {
			pr_fail("%s: 4 x 32 bit vector checksum mismatch, got 0x%8.8" PRIx32
				", expected 0x%" PRIx32 "\n", args->name, csum32, csum32_val);
			rc = EXIT_FAILURE;
			break;
		}

		csum64 = a64[0] ^ a64[1];
		stress_uint64_put(csum64);
		if (UNLIKELY(csum64 != csum64_val)) {
			pr_fail("%s: 2 x 64 bit vector checksum mismatch, got 0x%16.16" PRIx64
				", expected 0x%" PRIx64 "\n", args->name, csum64, csum64_val);
			rc = EXIT_FAILURE;
			break;
		}

#if defined(HAVE_INT128_T)
		csum128lo = (uint64_t)(a128[0] & 0xffffffffffffffffULL);
		csum128hi = (uint64_t)(a128[0] >> 64);
		stress_uint128_put(a128[0]);
		if (UNLIKELY((csum128lo != csum128lo_val) || (csum128hi != csum128hi_val))) {
			pr_fail("%s: 1 x 128 bit vector checksum mismatch, got 0x%16.16" PRIx64 "%16.16" PRIx64
				", expected 0x%16.16" PRIx64 "%16.16" PRIx64 "\n", args->name,
				csum128hi, csum128lo, csum128hi_val, csum128lo_val);
			rc = EXIT_FAILURE;
			break;
		}
#endif
		stress_bogo_inc(args);
	} while (stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	return rc;
}

const stressor_info_t stress_veccmp_info = {
	.stressor = stress_veccmp,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#else
const stressor_info_t stress_veccmp_info = {
	.stressor = stress_unimplemented,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE | CLASS_VECTOR,
	.verify = VERIFY_ALWAYS,
	.help = help
};
#endif
