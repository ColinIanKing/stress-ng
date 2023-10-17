/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
 * Copyright (C) 2022-2023 Colin Ian King.
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
#include "core-target-clones.h"
#include "core-mwc.h"

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

#define STRESS_USE_MWC_32

/* MWC random number initial seed */
#define STRESS_MWC_SEED_W	(521288629UL)
#define STRESS_MWC_SEED_Z	(362436069UL)

/* Fast random number generator state */
typedef struct {
#if defined(STRESS_USE_MWC_32)
	uint32_t w;
	uint32_t z;
#else
	uint64_t state;
#endif
	uint32_t n16;
	uint32_t saved16;
	uint32_t n8;
	uint32_t saved8;
	uint32_t n1;
	uint32_t saved1;
} stress_mwc_t;

static stress_mwc_t mwc = {
#if defined(STRESS_USE_MWC_32)
	STRESS_MWC_SEED_W,
	STRESS_MWC_SEED_Z,
#else
	((uint64_t)STRESS_MWC_SEED_W << 32) | (STRESS_MWC_SEED_Z),
#endif
	0,
	0,
	0,
	0,
	0,
	0,
};

static inline void mwc_flush(void)
{
	mwc.n16 = 0;
	mwc.n8 = 0;
	mwc.n1 = 0;
	mwc.saved16 = 0;
	mwc.saved8 = 0;
	mwc.saved1 = 0;
}

#if defined(HAVE_SYS_AUXV_H) && \
    defined(HAVE_GETAUXVAL) && \
    defined(AT_RANDOM)

#define VAL(ptr, n)	(((uint64_t)(*(ptr + n))) << (n << 3))

/*
 *  stress_aux_random_seed()
 *	get a fixed random value via getauxval
 */
static uint64_t stress_aux_random_seed(void)
{
	const uint8_t *ptr = (const uint8_t *)(uintptr_t)getauxval(AT_RANDOM);
	uint64_t val;

	if (!ptr)
		return 0ULL;

	val = VAL(ptr, 0) | VAL(ptr, 1) | VAL(ptr, 2) | VAL(ptr, 3) |
	      VAL(ptr, 4) | VAL(ptr, 5) | VAL(ptr, 6) | VAL(ptr, 7);

	return val;
}
#else
static uint64_t stress_aux_random_seed(void)
{
	return 0ULL;
}
#endif

/*
 *  stress_mwc_reseed()
 *	dirty mwc reseed, this is expensive as it
 *	pulls in various system values for the seeding
 */
void stress_mwc_reseed(void)
{
	if (g_opt_flags & OPT_FLAGS_SEED) {
		uint64_t seed;

		if (stress_get_setting("seed", &seed)) {
#if defined(STRESS_USE_MWC_32)
			mwc.z = seed >> 32;
			mwc.w = seed & 0xffffffff;
#else
			mwc.state = seed;
#endif
			mwc_flush();
			return;
		} else {
			pr_inf("mwc_core: cannot determine seed from --seed option\n");
			g_opt_flags &= ~(OPT_FLAGS_SEED);
		}
	}
	if (g_opt_flags & OPT_FLAGS_NO_RAND_SEED) {
#if defined(STRESS_USE_MWC_32)
		mwc.w = STRESS_MWC_SEED_W;
		mwc.z = STRESS_MWC_SEED_Z;
#else
		mwc.state = ((uint64_t)STRESS_MWC_SEED_W << 32) | (STRESS_MWC_SEED_Z);
#endif
	} else {
		struct timeval tv;
		struct rusage r;
		double m1, m5, m15;
		int i, n;
		const uint64_t aux_rnd = stress_aux_random_seed();
		const intptr_t p1 = (intptr_t)&mwc;
		const intptr_t p2 = (intptr_t)&tv;

#if defined(STRESS_USE_MWC_32)
		mwc.z = aux_rnd >> 32;
		mwc.w = aux_rnd & 0xffffffff;
#else
		mwc.state = aux_rnd;
#endif
		if (gettimeofday(&tv, NULL) == 0)
#if defined(STRESS_USE_MWC_32)
			mwc.z ^= (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
#else
			mwc.state ^= (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
#endif
#if defined(STRESS_USE_MWC_32)
		mwc.z += ~(p1 - p2);
		mwc.w += (uint64_t)getpid() ^ (uint64_t)getppid() << 12;
#else
		mwc.state += ~(p1 - p2);
		mwc.state += (uint64_t)getpid() ^ (uint64_t)getppid() << 12;
#endif
		if (stress_get_load_avg(&m1, &m5, &m15) == 0) {
#if defined(STRESS_USE_MWC_32)
			mwc.z += (uint64_t)(128.0 * (m1 + m15));
			mwc.w += (uint64_t)(256.0 * (m5));
#else
			mwc.state += (128.0 * (m1 + m15));
			mwc.state += ((uint64_t)(256.0 * (m5))) << 32;
#endif
		}
		if (getrusage(RUSAGE_SELF, &r) == 0) {
#if defined(STRESS_USE_MWC_32)
			mwc.z += r.ru_utime.tv_usec;
			mwc.w += r.ru_utime.tv_sec;
#else
			mwc.state += r.ru_utime.tv_usec;
			mwc.state += (uint64_t)r.ru_utime.tv_sec << 32;
#endif
		}
#if defined(STRESS_USE_MWC_32)
		mwc.z ^= stress_get_cpu();
		mwc.w ^= stress_get_phys_mem_size();
#else
		mwc.state ^= stress_get_cpu();
		mwc.state ^= stress_get_phys_mem_size();
#endif

#if defined(STRESS_USE_MWC_32)
		n = (int)mwc.z % 1733;
#else
		n = (int)(mwc.state & 0xffffffff) % 1733;
#endif
		for (i = 0; i < n; i++) {
			(void)stress_mwc32();
		}
	}
	mwc_flush();
}

/*
 *  stress_mwc_set_seed()
 *      set mwc seeds
 */
void stress_mwc_set_seed(const uint32_t w, const uint32_t z)
{
#if defined(STRESS_USE_MWC_32)
	mwc.w = w;
	mwc.z = z;
#else
	mwc.state = ((uint64_t)w << 32) | z;
#endif
	mwc_flush();
}

/*
 *  stress_mwc_get_seed()
 *      get mwc seeds
 */
void stress_mwc_get_seed(uint32_t *w, uint32_t *z)
{
#if defined(STRESS_USE_MWC_32)
	*w = mwc.w;
	*z = mwc.z;
#else
	*w = mwc.state >> 32;
	*z = mwc.state & 0xffffffff;
#endif
}

/*
 *  stress_mwc_seed()
 *      set default mwc seed
 */
void stress_mwc_seed(void)
{
	stress_mwc_set_seed(STRESS_MWC_SEED_W, STRESS_MWC_SEED_Z);
}


#if defined(STRESS_USE_MWC_32)
/*
 *  stress_mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
HOT OPTIMIZE3 inline uint32_t stress_mwc32(void)
{
	mwc.z = 36969 * (mwc.z & 65535) + (mwc.z >> 16);
	mwc.w = 18000 * (mwc.w & 65535) + (mwc.w >> 16);

	return (mwc.z << 16) + mwc.w;
}
#else
/*
 *  stress_mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, using 64 bit
 *	multiply
 */
HOT OPTIMIZE3 uint32_t stress_mwc32(void)
{
	register uint32_t c = (mwc.state) >> 32;
	register uint32_t x = (uint32_t)(mwc.state);
	register uint32_t r = x ^ c;

	mwc.state = x * ((uint64_t)4294883355UL) + c;
	return r;
}
#endif

/*
 *  stress_mwc64()
 *	get a 64 bit pseudo random number
 */
HOT OPTIMIZE3 uint64_t stress_mwc64(void)
{
	return (((uint64_t)stress_mwc32()) << 32) | stress_mwc32();
}

/*
 *  stress_mwc16()
 *	get a 16 bit pseudo random number
 */
HOT OPTIMIZE3 uint16_t stress_mwc16(void)
{
	if (LIKELY(mwc.n16)) {
		mwc.n16--;
		mwc.saved16 >>= 16;
	} else {
		mwc.n16 = 1;
		mwc.saved16 = stress_mwc32();
	}
	return mwc.saved16 & 0xffff;
}

/*
 *  stress_mwc8()
 *	get an 8 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t stress_mwc8(void)
{
	if (LIKELY(mwc.n8)) {
		mwc.n8--;
		mwc.saved8 >>= 8;
	} else {
		mwc.n8 = 3;
		mwc.saved8 = stress_mwc32();
	}
	return mwc.saved8 & 0xff;
}

/*
 *  stress_mwc1()
 *	get an 1 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t stress_mwc1(void)
{
	if (LIKELY(mwc.n1)) {
		mwc.n1--;
		mwc.saved1 >>= 1;
	} else {
		mwc.n1 = 31;
		mwc.saved1 = stress_mwc32();
	}
	return mwc.saved1 & 0x1;
}

/*
 *  stress_mwc8modn()
 *	see https://research.kudelskisecurity.com/2020/07/28/the-definitive-guide-to-modulo-bias-and-how-to-avoid-it/
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
HOT OPTIMIZE3 static uint8_t stress_mwc8modn_nonzero(const uint8_t max)
{
	register uint8_t threshold = max;
	register uint8_t val;

	while (threshold < 0x80U) {
		threshold <<= 1;
	}
	do {
		val = stress_mwc8();
	} while (val >= threshold);

	return val % max;
}

/*
 *  stress_mwc8modn()
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
HOT OPTIMIZE3 uint8_t stress_mwc8modn(const uint8_t max)
{
	return (LIKELY(max > 0)) ? stress_mwc8modn_nonzero(max) : 0;
}

/*
 *  stress_mwc8modn()
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 *	where max is potentially a power of 2
 */
HOT OPTIMIZE3 uint8_t stress_mwc8modn_maybe_pwr2(const uint8_t max)
{
	register const uint8_t mask = max - 1;

	if (UNLIKELY(max == 0))
		return 0;
	return ((max & mask) == 0) ?
		(stress_mwc8() & mask) : stress_mwc8modn_nonzero(max);
}

/*
 *  stress_mwc16modn()
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
HOT OPTIMIZE3 static uint16_t stress_mwc16modn_nonzero(const uint16_t max)
{
	register uint16_t threshold = max;
	register uint16_t val;

	while (threshold < 0x8000U) {
		threshold <<= 1;
	}
	do {
		val = stress_mwc16();
	} while (val >= threshold);

	return val % max;
}

/*
 *  stress_mwc16modn()
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
HOT OPTIMIZE3 uint16_t stress_mwc16modn(const uint16_t max)
{
	return (LIKELY(max > 0)) ? stress_mwc16modn_nonzero(max) : 0;
}

/*
 *  stress_mwc16modn()
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 *	where max is potentially a power of 2
 */
HOT OPTIMIZE3 uint16_t stress_mwc16modn_maybe_pwr2(const uint16_t max)
{
	register const uint16_t mask = max - 1;

	if (UNLIKELY(max == 0))
		return 0;
	return ((max & mask) == 0) ?
		(stress_mwc16() & mask) : stress_mwc16modn_nonzero(max);
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
HOT OPTIMIZE3 static uint32_t stress_mwc32modn_nonzero(const uint32_t max)
{
	register uint32_t threshold = max;
	register uint32_t val;

	while (threshold < 0x80000000UL) {
		threshold <<= 1;
	}
	do {
		val = stress_mwc32();
	} while (val >= threshold);

	return val % max;
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
HOT OPTIMIZE3 uint32_t stress_mwc32modn(const uint32_t max)
{
	return (LIKELY(max > 0)) ? stress_mwc32modn_nonzero(max) : 0;
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 *	where max is potentially a power of 2
 */
HOT OPTIMIZE3 uint32_t stress_mwc32modn_maybe_pwr2(const uint32_t max)
{
	register const uint32_t mask = max - 1;

	if (UNLIKELY(max == 0))
		return 0;
	return ((max & mask) == 0) ?
		(stress_mwc32() & mask) : stress_mwc32modn_nonzero(max);
}

/*
 *  stress_mwc64modn()
 *	return 64 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
HOT OPTIMIZE3 static uint64_t stress_mwc64modn_nonzero(const uint64_t max)
{
	register uint64_t threshold = max;
	register uint64_t val;

	if ((max & (max - 1)) == 0)
		return stress_mwc64() & (max - 1);

	while (threshold < 0x8000000000000000ULL) {
		threshold <<= 1;
	}
	do {
		val = stress_mwc64();
	} while (val >= threshold);

	return val % max;
}

/*
 *  stress_mwc64modn()
 *	return 64 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
HOT OPTIMIZE3 uint64_t stress_mwc64modn(const uint64_t max)
{
	return (LIKELY(max > 0)) ? stress_mwc64modn_nonzero(max) : 0;
}

/*
 *  stress_mwc64modn_maybe_pwr2()
 *	return 64 bit non-modulo biased value 1..max (inclusive)
 *	where max is potentially a power of 2
 */
HOT OPTIMIZE3 uint64_t stress_mwc64modn_maybe_pwr2(const uint64_t max)
{
	register const uint64_t mask = max - 1;

	if (UNLIKELY(max == 0))
		return 0;
	return ((max & mask) == 0) ?
		(stress_mwc64() & mask) : stress_mwc64modn_nonzero(max);
}
