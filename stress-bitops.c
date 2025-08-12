/*
 * Copyright (C)      2024 Colin Ian King.
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
#include "core-attribute.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-put.h"
#include "core-target-clones.h"

typedef int (*stress_bitops_func)(const char *name, uint32_t *count);

typedef struct {
	const char		*name;	/* human readable form of stressor */
	const stress_bitops_func	func;	/* the bitops method function */
} stress_bitops_method_info_t;

static const stress_help_t help[] = {
	{ NULL, "bitops N",		"start N workers that perform CPU only loading" },
	{ NULL, "bitops-method M",	"specify stress bitops method M, default is all" },
	{ NULL, "bitops-ops N",		"stop after N bitops bogo operations" },
	{ NULL,	 NULL,			NULL }
};

#if defined(HAVE_BUILTIN_CLZ)
#define BITOPS_CLZ(x)	(UNLIKELY((x) == 0) ? 32 : __builtin_clz((x)))
#endif

#if defined(HAVE_BUILTIN_CTZ)
#define BITOPS_CTZ(x)	(UNLIKELY((x) == 0) ? 32 : __builtin_ctz((x)))
#endif

static const stress_bitops_method_info_t bitops_methods[];

static int stress_bitops_all(const char *name, uint32_t *count);

/*
 *  stress_bitops_sign()
 *	return -1 if negative, 0 if positive
 */
static int OPTIMIZE3 stress_bitops_sign(const char *name, uint32_t *count)
{
	int32_t i;
	int32_t v = stress_mwc32();
	const uint32_t d = (~0U) >> 1;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		register int32_t sign1, sign2;

		/* #1 sign, comparison */
		sign1 = -(v < 0);
		sum += sign1;

		/* #2 sign, sign bit */
		sign2 = -(int)((unsigned int)((int)v) >> ((sizeof(int) * CHAR_BIT) - 1));
		sum += sign2;

		if (UNLIKELY(sign1 != sign2)) {
			pr_fail("%s: sign method failure, value %" PRId32
				", sign1 = %" PRId32 ", sign2 = %" PRId32 "\n",
				name, v, sign1, sign2);
			return EXIT_FAILURE;
		}
		v += d;
	}
	stress_uint32_put(sum);
	*count += (2 * i);

	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_abs()
 *	absolute value
 */
static int OPTIMIZE3 stress_bitops_abs(const char *name, uint32_t *count)
{
	int32_t i;
	int32_t v = stress_mwc32();
	const uint32_t d = (~0U) >> 1;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		register const int32_t mask = v >> (((sizeof(int) * CHAR_BIT)) - 1);
		register int32_t abs1, abs2;

		/* #1 abs, mask method 1 */
		abs1 = (v + mask) ^ mask;
		sum += abs1;

		/* #2 abs, mask method 1 */
		abs2 = (v ^ mask) - mask;
		sum += abs2;

		if (UNLIKELY(abs1 != abs2)) {
			pr_fail("%s: abs method failure, value %" PRId32
				", abs1 = %" PRId32 ", abs2 = %" PRId32 "\n",
				name, v, abs1, abs2);
			return EXIT_FAILURE;
		}
		v += d;
	}
	stress_uint32_put(sum);
	*count += (2 * i);
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_countbits()
 *	count number of bits set
 */
static int OPTIMIZE3 TARGET_CLONES stress_bitops_countbits(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t c1, c2, tmp;

		/* #1, Count bits, naive method */
		for (tmp = v, c1 = 0; tmp; tmp >>= 1)
			c1 += (tmp & 1);
		sum += c1;

		/* #2 Count bits, Brian Kernighan method */
		for (tmp = v, c2 = 0; tmp; c2++)
			tmp &= (tmp - 1);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: countbits Kernighan method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

		/* #3 Count bits 64 bit method */
		c2 = ((v & 0xfff) * 0x1001001001001ULL & 0x84210842108421ULL) % 0x1f;
		c2 += (((v & 0xfff000) >> 12) * 0x1001001001001ULL & 0x84210842108421ULL) % 0x1f;
		c2 += ((v >> 24) * 0x1001001001001ULL & 0x84210842108421ULL) % 0x1f;
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: countbits 64 bit method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

		/* #4 Count bits parallel method */
		tmp = v - ((v >> 1) & 0x55555555);
		tmp = (tmp & 0x33333333) + ((tmp >> 2) & 0x33333333);
		c2 = (((tmp + (tmp >> 4)) & 0xF0F0F0F) * 0x1010101) >> 24;
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: countbits parallel method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_POPCOUNT)
		/* #5 Count bits popcount method */
		c2 = __builtin_popcount((unsigned int)v);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: countbits builtin_popcount failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif

		/* #6 Count bits triple mask method */
		{
			const uint32_t ones = ~(uint32_t)0;
			const uint32_t mask1 = (ones / 3) << 1;
			const uint32_t mask2 = ones / 5;
			const uint32_t mask4 = ones / 17;

			c2 = v - ((mask1 & v) >> 1);
			c2 = (c2 & mask2) + ((c2 >> 2) & mask2);
			c2 = (c2 + (c2 >> 4)) & mask4;
			c2 += c2 >> 8;
			c2 += c2 >> 16;
			c2 &=  0xff;

			if (UNLIKELY(c1 != c2)) {
				pr_fail("%s: countbits triple mask failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
					name, v, c1, c2);
				return EXIT_FAILURE;
			}
		}
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_POPCOUNT)
	*count += 6 * i;
#else
	*count += 5 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_clz()
 *	count leading zeros
 */
static int OPTIMIZE3 TARGET_CLONES stress_bitops_clz(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t c1, c2, tmp, n;

		if (v == 0) {
			c1 = 32;
		} else {
			/* #1 Count leading zeros, naive method */
			for (c1 = 0, tmp = v; tmp && ((tmp & 0x80000000) == 0); tmp <<= 1)
				c1++;
		}
		sum += c1;

		/* #2 Count leading zeros, log shift method */
		n = 32;
		c2 = v;
		tmp = c2 >> 16;
		if (tmp != 0) {
			n -= 16;
			c2 = tmp;
		}
		tmp = c2 >> 8;
		if (tmp != 0) {
			n -= 8;
			c2 = tmp;
		}
		tmp = c2 >> 4;
		if (tmp != 0) {
			n -= 4;
			c2 = tmp;
		}
		tmp = c2 >> 2;
		if (tmp != 0) {
			n -= 2;
			c2 = tmp;
		}
		tmp = c2 >> 1;
		if (tmp != 0)
			c2 = n - 2;
		else
			c2 = n - c2;
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: clz log shift method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_POPCOUNT)
		/* #3 Count leading zeros, popcount method */
		tmp = v;
		tmp = tmp | (tmp >> 1);
		tmp = tmp | (tmp >> 2);
		tmp = tmp | (tmp >> 4);
		tmp = tmp | (tmp >> 8);
		tmp = tmp | (tmp >>16);
		c2 = __builtin_popcount((unsigned int)~tmp);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: clz builtin_popcount method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif
#if defined(HAVE_BUILTIN_CLZ)
		/* #4 Count leading zeros, clz method */
		c2 = BITOPS_CLZ(v);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: clz builtin_clz method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_POPCOUNT) &&	\
    defined(HAVE_BUILTIN_CLZ)
	*count += 4 * i;
#elif defined(HAVE_BUILTIN_POPCOUNT)
	*count += 3 * i;
#elif defined(HAVE_BUILTIN_CLZ)
	*count += 3 * i;
#else
	*count += 2 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_ctz()
 *	count trailing zeros
 */
static int OPTIMIZE3 TARGET_CLONES stress_bitops_ctz(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t c1, c2, tmp, n;
		uint32_t b0, b1, b2, b3, b4, bz;

		/* #1 Count trailing zeros, naive method */
		if (UNLIKELY(v == 0)) {
			c1 = 32;
		} else {
			for (c1 = 0, tmp = v; tmp && ((tmp & 1) == 0); tmp >>= 1)
				c1++;
		}
		sum += c1;

		/* #2 Count trailing zeros, mask and shift */
		if (UNLIKELY(v == 0)) {
			c2 = 32;
		} else {
			n = 1;
			tmp = v;
			if ((tmp & 0x0000ffff) == 0) {
				n += 16;
				tmp >>= 16;
			}
			if ((tmp & 0x000000ff) == 0) {
				n += 8;
				tmp >>= 8;
			}
			if ((tmp & 0x0000000f) == 0) {
				n += 4;
				tmp >>= 4;
			}
			if ((tmp & 0x00000003) == 0) {
				n += 2;
				tmp >>= 2;
			}
			c2 = n - (tmp & 1);
		}
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: ctz mask and shift method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

		/* #3 Count trailing zeros, Gaudet method */
		tmp = v & -v;
		bz = tmp ? 0 : 1;
		b4 = (tmp & 0x0000ffff) ? 0 : 16;
		b3 = (tmp & 0x00ff00ff) ? 0 : 8;
		b2 = (tmp & 0x0f0f0f0f) ? 0 : 4;
		b1 = (tmp & 0x33333333) ? 0 : 2;
		b0 = (tmp & 0x55555555) ? 0 : 1;
		c2 = bz + b4 + b3 + b2 + b1 + b0;
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: ctz Gaudet method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_CTZ)
		/* #4, Count trailing zeros, ctz method */
		c2 = BITOPS_CTZ((unsigned int)v);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: ctz builtin_ctz method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif
#if defined(HAVE_BUILTIN_POPCOUNT)
		/* #5, Count trailing zeros, popcount method */
		c2 = __builtin_popcount((unsigned int)((v & -v) - 1));
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: ctz builtin_popcount method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_CLZ) &&	\
    defined(HAVE_BUILTIN_POPCOUNT)
	*count += 5 * i;
#elif defined(HAVE_BUILTIN_CLZ)
	*count += 4 * i;
#elif defined(HAVE_BUILTIN_POPCOUNT)
	*count += 4 * i;
#else
	*count += 3 * i;
#endif

	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_cmp()
 *	compare, x < y -> -1, x = y -> 0, x > y -> 1
 */
static int OPTIMIZE3 stress_bitops_cmp(const char *name, uint32_t *count)
{
	int32_t i;
	int32_t x = stress_mwc32();
	int32_t y = x;
	const uint32_t dx = (~0U) >> 1;
	const uint32_t dy = (~0U) >> 2;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		int32_t cmp1, cmp2;

		/* #1 simple comparisons */
		if (x < y)
			cmp1 = -1;
		else if (x > y)
			cmp1 = 1;
		else
			cmp1 = 0;
		sum += cmp1;

		/* #2 branchless comparisons */
		cmp2 = (x > y) - (x < y);
		sum += cmp2;
		if (UNLIKELY(cmp1 != cmp2)) {
			pr_fail("%s: cmp method 1 failure, values 0x%" PRIx32 " vs 0x%" PRIx32 ", cmp1 = 0x%" PRIx32 ", cmp2 = 0x%" PRIx32 "\n",
				name, x, y, cmp1, cmp2);
			return EXIT_FAILURE;
		}

		/* #3 branchless comparisons */
		cmp2 = (x >= y) - (x <= y);
		sum += cmp2;
		if (UNLIKELY(cmp1 != cmp2)) {
			pr_fail("%s: cmp method 2 failure, values 0x%" PRIx32 " vs 0x%" PRIx32 ", cmp1 = 0x%" PRIx32 ", cmp2 = 0x%" PRIx32 "\n",
				name, x, y, cmp1, cmp2);
			return EXIT_FAILURE;
		}

		x += dx;
		y += dy;
	}
	stress_uint32_put(sum);
	*count += 3 * i;

	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_parity()
 *	parity
 */
static int OPTIMIZE3 stress_bitops_parity(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		bool p1, p2;
		uint32_t tmp;

		/* #1 Parity, very naive method */
		for (p1 = 0, tmp = v; tmp; tmp >>= 1)
			p1 = (tmp & 1) ? !p1 : p1;
		sum += p1;

		/* #2 Parity, naive method */
		for (p2 = 0, tmp = v; tmp; tmp = tmp & (tmp - 1))
			p2 = !p2;
		sum += p2;
		if (UNLIKELY(p1 != p2)) {
			pr_fail("%s: parity naive method failure, value 0x%" PRIx32 ", p1 = 0x%x, p2 = 0x%x\n",
				name, v, p1, p2);
			return EXIT_FAILURE;
		}

		/* #3 Parity, multiplication */
		tmp = v ^ (v >> 1);
		tmp ^= tmp >> 2;
		tmp = (tmp & 0x11111111U) * 0x11111111U;
		p2 = (tmp >> 28) & 1;
		sum += p2;
		if (p1 != p2)  {
			pr_fail("%s: parity 32 bit multiply method failure, value 0x%" PRIx32 ", p1 = 0x%x, p2 = 0x%x\n",
				name, v, p1, p2);
			return EXIT_FAILURE;
		}

		/* #4 Parity, xor and shifting */
		tmp = v ^ (v >> 16);
		tmp ^= tmp >> 8;
		tmp ^= tmp >> 4;
		tmp &= 0xf;
		p2 = (0x6996 >> tmp) & 1;
		sum += p2;
		if (UNLIKELY(p1 != p2)) {
			pr_fail("%s: parity parallel method failure, value 0x%" PRIx32 ", p1 = 0x%x, p2 = 0x%x\n",
				name, v, p1, p2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_PARITY)
		/* #5 Parity, builtin */
		p2 = __builtin_parity((unsigned int)v);
		sum += p2;
		if (UNLIKELY(p1 != p2)) {
			pr_fail("%s: parity builtin_parity method failure, value 0x%" PRIx32 ", p1 = 0x%x, p2 = 0x%x\n",
				name, v, p1, p2);
			return EXIT_FAILURE;
		}
#endif
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_PARITY)
	*count += 5 * i;
#else
	*count += 4 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_min()
 *	minimum of x, y
 */
static int OPTIMIZE3 stress_bitops_min(const char *name, uint32_t *count)
{
	int32_t i;
	int32_t x = stress_mwc32();
	int32_t y = stress_mwc32();
	const uint32_t dx = (~0U) >> 1;
	const uint32_t dy = (~0U) >> 2;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		register int32_t min1, min2;

		min1 = y ^ ((x ^ y) & -(x < y));
		sum += min1;

		min2 = (x < y) ? x : y;
		sum += min2;

		if (UNLIKELY(min1 != min2)) {
			pr_fail("%s: min method failure, values %" PRId32 " %" PRId32
				", min1 = %" PRId32 ", min2 = %" PRId32 "\n",
				name, x, y, min1, min2);
			return EXIT_FAILURE;
		}
		x += dx;
		y += dy;
	}
	stress_uint32_put(sum);
	*count += 2 * i;
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_max()
 *	maximum of x, y
 */
static int OPTIMIZE3 stress_bitops_max(const char *name, uint32_t *count)
{
	int32_t i;
	int32_t x = stress_mwc32();
	int32_t y = stress_mwc32();
	const uint32_t dx = (~0U) >> 1;
	const uint32_t dy = (~0U) >> 2;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		register int32_t max1, max2;

		max1 = x ^ ((x ^ y) & -(x < y));
		sum += max1;

		max2 = (x > y) ? x : y;
		sum += max2;

		if (UNLIKELY(max1 != max2)) {
			pr_fail("%s: max method failure, values %" PRId32 " %" PRId32
				", max1 = %" PRId32 ", max2 = %" PRId32 "\n",
				name, x, y, max1, max2);
			return EXIT_FAILURE;
		}
		x += dx;
		y += dy;
	}
	stress_uint32_put(sum);
	*count += 2 * i;
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_log2()
 *	log base 2
 */
static int OPTIMIZE3 stress_bitops_log2(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16() << 12;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		static const int8_t bitposition[32] ALIGN64 = {
			 0,  9,  1, 10, 13, 21,  2, 29,
			11, 14, 16, 18, 22, 25,  3, 30,
			 8, 12, 20, 28, 15, 17, 24, 7,
			19, 27, 23,  6, 26,  5,  4, 31
		};
		uint32_t ln2_1, ln2_2, tmp, shift;

		tmp = v;
		ln2_1 = 0;
		while (tmp >>= 1)
			ln2_1++;
		sum += ln2_1;

		tmp = v;
		ln2_2 = 0;
		if (tmp & 0xffff0000) {
			tmp >>= 16;
			ln2_2 |= 16;
		}
		if (tmp & 0xff00) {
			tmp >>= 8;
			ln2_2 |= 8;
		}
		if (tmp & 0xf0) {
			tmp >>= 4;
			ln2_2 |= 4;
		}
		if (tmp & 0xc) {
			tmp >>= 2;
			ln2_2 |= 2;
		}
		if (tmp & 0x2) {
			ln2_2 |= 1;
		}
		sum += ln2_2;
		if (UNLIKELY(ln2_1 != ln2_2)) {
			pr_fail("%s: log2 mask and shift method 1 failure, value 0x%" PRIx32 ", ln2_1 = 0x%" PRIx32 ", ln2_2 = 0x%" PRIx32 "\n",
				name, v, ln2_1, ln2_2);
			return EXIT_FAILURE;
		}

		tmp = v;
		ln2_2 = (tmp > 0xffff) << 4;
		tmp >>= ln2_2;
		shift = (tmp > 0xff) << 3;
		tmp >>= shift;
		ln2_2 |= shift;
		shift = (tmp > 0xf) << 2;
		tmp >>= shift;
		ln2_2 |= shift;
		shift = (tmp > 0x3) << 1;
		tmp >>= shift;
		ln2_2 |= shift | (tmp >> 1);
		sum += ln2_2;
		if (UNLIKELY(ln2_1 != ln2_2)) {
			pr_fail("%s: log2 mask and shift method 2 failure, value 0x%" PRIx32 ", ln2_1 = 0x%" PRIx32 ", ln2_2 = 0x%" PRIx32 "\n",
				name, v, ln2_1, ln2_2);
			return EXIT_FAILURE;
		}

		tmp = v;
		tmp |= tmp >> 1;
		tmp |= tmp >> 2;
		tmp |= tmp >> 4;
		tmp |= tmp >> 8;
		tmp |= tmp >> 16;
		ln2_2 = bitposition[(uint32_t)(tmp * 0x07c4acdd) >> 27];
		sum += ln2_2;
		if (UNLIKELY(ln2_1 != ln2_2)) {
			pr_fail("%s: log2 multiply and lookup method failure, value 0x%" PRIx32 ", ln2_1 = 0x%" PRIx32 ", ln2_2 = 0x%" PRIx32 "\n",
				name, v, ln2_1, ln2_2);
			return EXIT_FAILURE;
		}

		v += dv;
	}
	stress_uint32_put(sum);
	*count += 4 * i;
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_reverse()
 *	bit reverse
 */
static int OPTIMIZE3 stress_bitops_reverse(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = stress_mwc32();
	const uint32_t dv = stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t tmp, r1, r2, s, mask;
		uint8_t b1, b2, b3, b4;

		r1 = v;
		s = (sizeof(r1) * CHAR_BIT) - 1;
		for (tmp = v >> 1; tmp; tmp >>= 1) {
			r1 <<= 1;
			r1 |= (tmp & 1);
			s--;
		}
		r1 <<= s;
		sum += r1;

		mask = ~0;
		s = (sizeof(r2) * CHAR_BIT);
		r2 = v;
		while ((s >>= 1) > 0) {
			mask ^= (mask << s);
			r2 = ((r2 >> s) & mask) | ((r2 << s) & ~mask);
		}
		sum += r2;
		if (UNLIKELY(r1 != r2)) {
			pr_fail("%s: reverse lg(N) method failure, value 0x%" PRIx32 ", r1 = 0x%" PRIx32 ", r2 = 0x%" PRIx32 "\n",
				name, v, r1, r2);
			return EXIT_FAILURE;
		}

		tmp = v;
		tmp = (((tmp & 0xaaaaaaaaUL) >> 1)  | ((tmp & 0x55555555UL) << 1));
		tmp = (((tmp & 0xccccccccUL) >> 2)  | ((tmp & 0x33333333UL) << 2));
		tmp = (((tmp & 0xf0f0f0f0UL) >> 4)  | ((tmp & 0x0f0f0f0fUL) << 4));
		tmp = (((tmp & 0xff00ff00UL) >> 8)  | ((tmp & 0x00ff00ffUL) << 8));
		r2 =  (((tmp & 0xffff0000UL) >> 16) | ((tmp & 0x0000ffffUL) << 16));
		sum += r2;
		if (UNLIKELY(r1 != r2)) {
			pr_fail("%s: reverse parallel method failure, value 0x%" PRIx32 ", r1 = 0x%" PRIx32 ", r2 = 0x%" PRIx32 "\n",
				name, v, r1, r2);
			return EXIT_FAILURE;
		}

		b1 = v;
		b1 = ((b1 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
		b2 = v >> 8;
		b2 = ((b2 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
		b3 = v >> 16;
		b3 = ((b3 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
		b4 = v >> 24;
		b4 = ((b4 * 0x80200802ULL) & 0x0884422110ULL) * 0x0101010101ULL >> 32;
		r2 = ((uint32_t)b1 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | ((uint32_t)b4 << 0);
		sum += r2;
		if (UNLIKELY(r1 != r2)) {
			pr_fail("%s: reverse 64 bit multiply method failure, value 0x%" PRIx32 ", r1 = 0x%" PRIx32 ", r2 = 0x%" PRIx32 "\n",
				name, v, r1, r2);
			return EXIT_FAILURE;
		}

		b1 = v;
		b1 = ((b1 * 0x0802LU & 0x22110LU) | (b1 * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
		b2 = (v >> 8);
		b2 = ((b2 * 0x0802LU & 0x22110LU) | (b2 * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
		b3 = (v >> 16);
		b3 = ((b3 * 0x0802LU & 0x22110LU) | (b3 * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
		b4 = (v >> 24);
		b4 = ((b4 * 0x0802LU & 0x22110LU) | (b4 * 0x8020LU & 0x88440LU)) * 0x10101LU >> 16;
		r2 = ((uint32_t)b1 << 24) | ((uint32_t)b2 << 16) | ((uint32_t)b3 << 8) | ((uint32_t)b4 << 0);
		sum += r2;
		if (UNLIKELY(r1 != r2)) {
			pr_fail("%s: reverse non-64 bit multiply method failure, value 0x%" PRIx32 ", r1 = 0x%" PRIx32 ", r2 = 0x%" PRIx32 "\n",
				name, v, r1, r2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_BITREVERSE)
		r2 = __builtin_bitreverse32(v);
		sum += r2;
		if (UNLIKELY(r1 != r2)) {
			pr_fail("%s: reverse builtin_reverse method failure, value 0x%" PRIx32 ", r1 = 0x%" PRIx32 ", r2 = 0x%" PRIx32 "\n",
				name, v, r1, r2);
			return EXIT_FAILURE;
		}
#endif
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_BITREVERSE)
	*count += 6 * i;
#else
	*count += 5 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_pwr2()
 *	detect if integer is a power of 2
 */
static int OPTIMIZE3 stress_bitops_pwr2(const char *name, uint32_t *count)
{
	register uint32_t i, j = stress_mwc32();

	for (i = 0; i < 1000; i++, j += i) {
		register bool is_pwr2, result;

#if defined(HAVE_BUILTIN_POPCOUNT)
		is_pwr2 = (__builtin_popcount((unsigned int)j) == 1);
#else
		{
			uint32_t tmp, c;

			for (tmp = j, c = 0; tmp; c++)
				tmp &= (tmp - 1);

			is_pwr2 = (c == 1);
		}
#endif
		result = (j > 0) & ((j & (j - 1)) == 0);
		if (result != is_pwr2) {
			pr_fail("%s: pwr2 failure, value 0x%" PRIx32 ", r1 = 0x%x, r2 = 0x%x\n",
				name, i, is_pwr2, result);
			return EXIT_FAILURE;
		}
	}
	*count += i;

	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_rnddnpwr2()
 *	round down to nearest power of 2
 */
static int OPTIMIZE3 stress_bitops_rnddnpwr2(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = 0;
	uint32_t dv = 0x12345 + stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t c1, c2, tmp;

		/*
		 *  #1 rnddnpwr2: 1 << (31 - clz(v))
		 *		= 0x80000000 >> clz(v)
		 */
		if (v == 0) {
			c1 = 0;
		} else {
			for (c1 = 0, tmp = v; tmp && ((tmp & 0x80000000) == 0); tmp <<= 1)
				c1++;
			c1 = 0x80000000 >> c1;
		}
		sum += c1;

		/*  #2 rnddnpwr2 branch free */
		c2 = v;
		c2 |= (c2 >> 1);
		c2 |= (c2 >> 2);
		c2 |= (c2 >> 4);
		c2 |= (c2 >> 8);
		c2 |= (c2 >> 16);
		c2 = (c2 - (c2 >> 1));
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: rnddnpwr2 branch free method 1 failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_CTZ)
		/*  #3, rnddnpwr2 ctz */
		c2 = (v == 0) ? 0 : 0x80000000 >> BITOPS_CLZ(v);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: rnddnpwr2 clz method 1 failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif
		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_CTZ)
	*count += 3 * i;
#else
	*count += 2 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_rnduppwr2()
 *	round up to nearest power of 2
 */
static int OPTIMIZE3 stress_bitops_rnduppwr2(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t v = 0;
	uint32_t dv = 0x12345 + stress_mwc16();
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		uint32_t c1, c2, tmp;

		/*
		 *  #1 rnduppwr2: 1 << (31 - clz(v - 1))
		 *		= 0x80000000 >> clz(v - 1)
		 */
		switch (v) {
		case 0:
			c1 = 0;
			break;
		case 1:
			c1 = 1;
			break;
		default:
			for (c1 = 0, tmp = v - 1; tmp && ((tmp & 0x80000000) == 0); tmp <<= 1)
				c1++;
			c1 = (c1 > 0) ? 0x80000000 >> (c1 - 1) : 0;
			break;
		}
		sum += c1;

		/*  #2 rnduppwr2 branch free */
		c2 = v - 1;
		c2 |= (c2 >> 1);
		c2 |= (c2 >> 2);
		c2 |= (c2 >> 4);
		c2 |= (c2 >> 8);
		c2 |= (c2 >> 16);
		c2 += 1;
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: rmduppwr2 branch free method failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}

#if defined(HAVE_BUILTIN_CTZ)
		/*  #3, rnduppwr2 ctz */
		c2 = (v == 0) ? 0 : 0x80000000 >> (BITOPS_CLZ(v - 1) - 1);
		sum += c2;
		if (UNLIKELY(c1 != c2)) {
			pr_fail("%s: rnduppwr2 clz method 1 failure, value 0x%" PRIx32 ", c1 = 0x%" PRIx32 ", c2 = 0x%" PRIx32 "\n",
				name, v, c1, c2);
			return EXIT_FAILURE;
		}
#endif

		v += dv;
	}
	stress_uint32_put(sum);
#if defined(HAVE_BUILTIN_CTZ)
	*count += 3 * i;
#else
	*count += 2 * i;
#endif
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_swap()
 *	swap x with y
 */
static int OPTIMIZE3 stress_bitops_swap(const char *name, uint32_t *count)
{
	int32_t i;
	uint32_t x = stress_mwc32();
	uint32_t y = stress_mwc32();
	const uint32_t dx = (~0U) >> 1;
	const uint32_t dy = (~0U) >> 2;
	uint32_t sum = 0;

	for (i = 0; i < 1000; i++) {
		register uint32_t sx, sy;

		sx = x;
		sy = y;
		sx -= sy;
		sy += sx;
		sx = sy - sx;
		sum += sx + sy;
		if (UNLIKELY((sx != y) && (sy != x))) {
			pr_fail("%s: swap add/sub method failure, values %" PRIu32 " %" PRIu32
				", swapped %" PRIu32 " %" PRIu32 "\n",
				name, x, y, sx, sy);
			return EXIT_FAILURE;
		}

		sx = x;
		sy = y;
		sx ^= sy;
		sy ^= sx;
		sx ^= sy;
		sum += sx + sy;
		if (UNLIKELY((sx != y) && (sy != x))) {
			pr_fail("%s: swap xor method failure, values %" PRIu32 " %" PRIu32
				", swapped %" PRIu32 " %" PRIu32 "\n",
				name, x, y, sx, sy);
			return EXIT_FAILURE;
		}

		x += dx;
		y += dy;
	}
	stress_uint32_put(sum);
	*count += 2 * i;
	return EXIT_SUCCESS;
}

/*
 *  stress_bitops_zerobyte()
 *	check if an integer contains a zero byte
 */
static int OPTIMIZE3 stress_bitops_zerobyte(const char *name, uint32_t *count)
{
	register uint32_t i, j = stress_mwc32();

	for (i = 0; i < 1000; i++, j += i) {
		register bool has_zero_byte, result;

		has_zero_byte = ((((j & 0x000000ffU) == 0) |
				  ((j & 0x0000ff00U) == 0) |
				  ((j & 0x00ff0000U) == 0) |
				  ((j & 0xff000000U) == 0)) > 0);
		result = (((j - 0x01010101U) & (~j) & 0x80808080U) > 0);
		if (result != has_zero_byte) {
			pr_fail("%s: zerobyte failure, value 0x%" PRIx32 ", r1 = 0x%x, r2 = 0x%x\n",
				name, i, has_zero_byte, result);
			return EXIT_FAILURE;
		}
	}
	*count += i;
	return EXIT_SUCCESS;
}

/*
 * Table of bitops stress methods
 */
static const stress_bitops_method_info_t bitops_methods[] = {
	{ "all",		stress_bitops_all },	/* Special "all" test */
	{ "abs",		stress_bitops_abs },
	{ "countbits",		stress_bitops_countbits },
	{ "clz",		stress_bitops_clz },
	{ "ctz",		stress_bitops_ctz },
	{ "cmp",		stress_bitops_cmp },
	{ "log2",		stress_bitops_log2 },
	{ "max",		stress_bitops_max },
	{ "min",		stress_bitops_min },
	{ "parity",		stress_bitops_parity },
	{ "pwr2",		stress_bitops_pwr2 },
	{ "reverse",		stress_bitops_reverse },
	{ "rnddnpwr2",		stress_bitops_rnddnpwr2 },
	{ "rnduppwr2",		stress_bitops_rnduppwr2 },
	{ "sign",		stress_bitops_sign },
	{ "swap",		stress_bitops_swap },
	{ "zerobyte",		stress_bitops_zerobyte },
};

stress_metrics_t metrics[SIZEOF_ARRAY(bitops_methods)];

static const char *stress_bitops_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(bitops_methods)) ? bitops_methods[i].name : NULL;
}

static int stress_bitops_callfunc(const char *name, const size_t method)
{
	double t1, t2;
	uint32_t count = 0;
	int ret;

	t1 = stress_time_now();
	ret = bitops_methods[method].func(name, &count);
	t2 = stress_time_now();

	metrics[method].count += (double)count;
	metrics[method].duration += t2 - t1;

	return ret;
}

static int stress_bitops_all(const char *name, uint32_t *count)
{
	static size_t i = 1;
	int rc;
	(void)count;

	rc = stress_bitops_callfunc(name, i);
	i++;
	if (i >= SIZEOF_ARRAY(bitops_methods))
		i = 1;
	return rc;
}

/*
 *  stress_bitops()
 *	stress CPU by doing floating point math ops
 */
static int stress_bitops(stress_args_t *args)
{
	size_t bitops_method = 0;
	int rc = EXIT_SUCCESS;
	size_t i, j;

	stress_zero_metrics(metrics, SIZEOF_ARRAY(metrics));

	(void)stress_get_setting("bitops-method", &bitops_method);

	if (stress_instance_zero(args))
		pr_dbg("%s: using method '%s'\n", args->name, bitops_methods[bitops_method].name);

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	do {
		uint32_t count;

		rc = bitops_methods[bitops_method].func(args->name, &count);
		stress_bogo_inc(args);
	} while ((rc == EXIT_SUCCESS) && stress_continue(args));

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);

	for (i = 0, j = 0; i < SIZEOF_ARRAY(metrics); i++) {
		const double rate = metrics[i].duration > 0.0 ? metrics[i].count / metrics[i].duration : 0.0;

		if (rate > 0.0) {
			char buf[32];

			(void)snprintf(buf, sizeof(buf), "%s mega-ops per second", bitops_methods[i].name);
			stress_metrics_set(args, j, buf, rate / 1000000.0, STRESS_METRIC_GEOMETRIC_MEAN);
			j++;
		}
	}
	return rc;
}

static const stress_opt_t opts[] = {
	{ OPT_bitops_method, "bitops-method", TYPE_ID_SIZE_T_METHOD, 0, 0, stress_bitops_method },
	END_OPT,
};

const stressor_info_t stress_bitops_info = {
	.stressor = stress_bitops,
	.classifier = CLASS_CPU | CLASS_INTEGER | CLASS_COMPUTE,
	.opts = opts,
	.verify = VERIFY_ALWAYS,
	.help = help
};
