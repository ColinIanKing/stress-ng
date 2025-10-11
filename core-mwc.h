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
#ifndef CORE_MWC_H
#define CORE_MWC_H

#include <stdint.h>
#include "core-attribute.h"

/*
 *  Use modulo reduction, this uses multiplication and shift
 *  which is normally faster than mask and compare looping.
 *  32 bit systems generally don't have 128 unsigned integer
 *  multiplication support required for 64 bit modulo reduction
 *  so fast modulo reduction only works for 8, 16, 32 bits for
 *  these smaller systems.
 */
#define HAVE_FAST_MODULO_REDUCTION

extern void stress_mwc_reseed(void);
extern void stress_mwc_set_seed(const uint32_t w, const uint32_t z);
extern void stress_mwc_get_seed(uint32_t *w, uint32_t *z);
extern void stress_mwc_default_seed(void);

extern uint8_t stress_mwc1(void);
extern uint8_t stress_mwc8(void);
extern uint16_t stress_mwc16(void);
extern uint32_t stress_mwc32(void);
extern uint64_t stress_mwc64(void);


extern void stress_rndbuf(void *buf, const size_t len);
extern void stress_rndstr(char *str, const size_t len);
extern void stress_uint8rnd4(uint8_t *data, const size_t len);


#if defined(HAVE_FAST_MODULO_REDUCTION)
/*
 *  stress_mwc8modn()
 *	see https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
static inline uint8_t stress_mwc8modn(const uint8_t max)
{
	return (uint8_t)(((uint16_t)stress_mwc8() * (uint16_t)max) >> 8);
}

/*
 *  stress_mwc16modn()
 *	see https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 */
static inline uint16_t stress_mwc16modn(const uint16_t max)
{
	return (uint16_t)(((uint32_t)stress_mwc16() * (uint32_t)max) >> 16);
}

/*
 *  stress_mwc32modn()
 *	see https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 */
static inline uint32_t stress_mwc32modn(const uint32_t max)
{
	return (uint32_t)(((uint64_t)stress_mwc32() * (uint64_t)max) >> 32);
}
#else

extern uint8_t stress_mwc8modn(const uint8_t max);
extern uint16_t stress_mwc16modn(const uint16_t max);
extern uint32_t stress_mwc32modn(const uint32_t max);

#endif

#if defined(HAVE_FAST_MODULO_REDUCTION) &&	\
    defined(HAVE_INT128_T)
/*
 *  stress_mwc64modn()
 *	see https://lemire.me/blog/2016/06/27/a-fast-alternative-to-the-modulo-reduction
 *	return 64 bit non-modulo biased value 1..max (inclusive)
 */
static inline uint64_t stress_mwc64modn(const uint64_t max)
{
	return (uint64_t)(((__uint128_t)stress_mwc64() * (__uint128_t)max) >> 64);
}
#else
extern uint64_t stress_mwc64modn(const uint64_t max);
#endif

#endif
