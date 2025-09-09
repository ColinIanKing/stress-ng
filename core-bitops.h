/*
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
#ifndef CORE_BITOPS_H
#define CORE_BITOPS_H

#include "core-attribute.h"

/*
 *  stress_reverse64
 *	generic fast-ish 64 bit reverse
 */
static inline uint64_t CONST ALWAYS_INLINE stress_reverse64(register uint64_t x)
{
#if defined(HAVE_BUILTIN_BITREVERSE)
	return __builtin_bitreverse64(x);
#else
	x = (((x & 0xaaaaaaaaaaaaaaaaULL) >> 1)  | ((x & 0x5555555555555555ULL) << 1));
	x = (((x & 0xccccccccccccccccULL) >> 2)  | ((x & 0x3333333333333333ULL) << 2));
	x = (((x & 0xf0f0f0f0f0f0f0f0ULL) >> 4)  | ((x & 0x0f0f0f0f0f0f0f0fULL) << 4));
	x = (((x & 0xff00ff00ff00ff00ULL) >> 8)  | ((x & 0x00ff00ff00ff00ffULL) << 8));
	x = (((x & 0xffff0000ffff0000ULL) >> 16) | ((x & 0x0000ffff0000ffffULL) << 16));
	return ((x >> 32) | (x << 32));
#endif
}

/*
 *  stress_reverse32
 *	generic fast-ish 32 bit reverse
 */
static inline uint32_t CONST ALWAYS_INLINE stress_reverse32(register uint32_t x)
{
#if defined(HAVE_BUILTIN_BITREVERSE)
	return __builtin_bitreverse32(x);
#else
	x = (((x & 0xaaaaaaaaUL) >> 1)  | ((x & 0x55555555UL) << 1));
	x = (((x & 0xccccccccUL) >> 2)  | ((x & 0x33333333UL) << 2));
	x = (((x & 0xf0f0f0f0UL) >> 4)  | ((x & 0x0f0f0f0fUL) << 4));
	x = (((x & 0xff00ff00UL) >> 8)  | ((x & 0x00ff00ffUL) << 8));
	x = (((x & 0xffff0000UL) >> 16) | ((x & 0x0000ffffUL) << 16));
	return x;
#endif
}

/*
 *  stress_reverse16
 *	generic fast-ish 16 bit reverse
 */
static inline uint16_t CONST ALWAYS_INLINE stress_reverse16(register uint16_t x)
{
#if defined(HAVE_BUILTIN_BITREVERSE)
	return __builtin_bitreverse16(x);
#else
	x = (((x & 0xaaaaUL) >> 1)  | ((x & 0x5555UL) << 1));
	x = (((x & 0xccccUL) >> 2)  | ((x & 0x3333UL) << 2));
	x = (((x & 0xf0f0UL) >> 4)  | ((x & 0x0f0fUL) << 4));
	x = (((x & 0xff00UL) >> 8)  | ((x & 0x00ffUL) << 8));
	return x;
#endif
}

/*
 *  stress_reverse8
 *	generic fast-ish 8 bit reverse
 */
static inline uint8_t CONST ALWAYS_INLINE stress_reverse8(register uint8_t x)
{
#if defined(HAVE_BUILTIN_BITREVERSE)
	return __builtin_bitreverse8(x);
#else
	x = (((x & 0xaaUL) >> 1)  | ((x & 0x55UL) << 1));
	x = (((x & 0xccUL) >> 2)  | ((x & 0x33UL) << 2));
	x = (((x & 0xf0UL) >> 4)  | ((x & 0x0fUL) << 4));
	return x;
#endif
}

/*
 *  stress_swap32()
 *	swap order of bytes of a uint32_t value
 */
static inline uint32_t CONST ALWAYS_INLINE stress_swap32(uint32_t val)
{
#if defined(HAVE_BUILTIN_BSWAP32)
	return __builtin_bswap32(val);
#else
	return ((val >> 24) & 0x000000ff) |
	       ((val << 8)  & 0x00ff0000) |
	       ((val >> 8)  & 0x0000ff00) |
	       ((val << 24) & 0xff000000);
#endif
}

/*
 *  stress_bitreverse32()
 *	reverse bits in a uint32_t value
 */
static inline uint32_t CONST ALWAYS_INLINE stress_bitreverse32(const uint32_t val)
{
#if defined(HAVE_BUILTIN_BITREVERSE)
	return  __builtin_bitreverse32(val);
#else
	register uint32_t r, v, s = (sizeof(val) * 8) - 1;

	r = v = val;
	for (v >>= 1; v; v >>= 1, s--) {
		r <<= 1;
		r |= v & 1;
	}
	r <<= s;

	return r;
#endif
}

/*
 *  stress_popcount64()
 *	population count (count number of 1 bits) in a uint64_t value
 */
static inline uint32_t CONST ALWAYS_INLINE stress_popcount64(const uint64_t val)
{
#if defined(HAVE_BUILTIN_POPCOUNT) &&	\
    !defined(HAVE_COMPILER_PCC)
	return  (uint32_t)__builtin_popcountll(val);
#else
	/* Brian Kernighan's count bits */
	register uint64_t j, v = val;

	for (j = 0; v; j++)
		v &= v - 1;
	return j;
#endif
}

/*
 *  stress_popcount32()
 *	population count (count number of 1 bits) in a uint32_t value
 */
static inline uint32_t CONST ALWAYS_INLINE stress_popcount32(const uint32_t val)
{
#if defined(HAVE_BUILTIN_POPCOUNT)
	return  __builtin_popcount((unsigned int)val);
#else
	/* Brian Kernighan's count bits */
	register uint32_t j, v = val;

	for (j = 0; v; j++)
		v &= v - 1;
	return j;
#endif
}

/*
 *  stress_parity32
 *	parity check in a uint32_t value
 */
static inline uint32_t CONST ALWAYS_INLINE stress_parity32(const uint32_t val)
{
#if defined(HAVE_BUILTIN_PARITY)
	return  __builtin_parity((unsigned int)val);
#else
	/* parity check */
	register uint32_t v = val;

	v ^= v >> 16;
	v ^= v >> 8;
	v ^= v >> 4;
	v &= 0xf;
	return (0x6996 >> v) & 1;
#endif
}

/*
 *  stress_nextpwr2()
 *	round to next power of 2
 */
static inline uint32_t CONST ALWAYS_INLINE stress_nextpwr2(const uint32_t val)
{
	register uint32_t v = val - 1;

	v |= v >> 1;
	v |= v >> 2;
	v |= v >> 4;
	v |= v >> 8;
	v |= v >> 16;
	return v + 1;
}
#endif
