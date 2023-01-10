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

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

/* MWC random number initial seed */
#define STRESS_MWC_SEED_Z	(362436069UL)
#define STRESS_MWC_SEED_W	(521288629UL)

/* Fast random number generator state */
typedef struct {
	uint32_t w;
	uint32_t z;
} stress_mwc_t;

static stress_mwc_t mwc = {
	STRESS_MWC_SEED_W,
	STRESS_MWC_SEED_Z
};

static uint8_t mwc_n1, mwc_n8, mwc_n16;

static inline void mwc_flush(void)
{
	mwc_n1 = 0;
	mwc_n8 = 0;
	mwc_n16 = 0;
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
			mwc.z = seed >> 32;
			mwc.w = seed & 0xffffffff;
			mwc_flush();
			return;
		} else {
			pr_inf("mwc_core: cannot determine seed from --seed option\n");
			g_opt_flags &= ~(OPT_FLAGS_SEED);
		}
	}
	if (g_opt_flags & OPT_FLAGS_NO_RAND_SEED) {
		mwc.w = STRESS_MWC_SEED_W;
		mwc.z = STRESS_MWC_SEED_Z;
	} else {
		struct timeval tv;
		struct rusage r;
		double m1, m5, m15;
		int i, n;
		const uint64_t aux_rnd = stress_aux_random_seed();
		const intptr_t p1 = (intptr_t)&mwc.z;
		const intptr_t p2 = (intptr_t)&tv;

		mwc.z = aux_rnd >> 32;
		mwc.w = aux_rnd & 0xffffffff;
		if (gettimeofday(&tv, NULL) == 0)
			mwc.z ^= (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
		mwc.z += ~(p1 - p2);
		mwc.w += (uint64_t)getpid() ^ (uint64_t)getppid()<<12;
		if (stress_get_load_avg(&m1, &m5, &m15) == 0) {
			mwc.z += (uint64_t)(128.0 * (m1 + m15));
			mwc.w += (uint64_t)(256.0 * (m5));
		}
		if (getrusage(RUSAGE_SELF, &r) == 0) {
			mwc.z += r.ru_utime.tv_usec;
			mwc.w += r.ru_utime.tv_sec;
		}
		mwc.z ^= stress_get_cpu();
		mwc.w ^= stress_get_phys_mem_size();

		n = (int)mwc.z % 1733;
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
	mwc.w = w;
	mwc.z = z;

	mwc_flush();
}

/*
 *  stress_mwc_get_seed()
 *      get mwc seeds
 */
void stress_mwc_get_seed(uint32_t *w, uint32_t *z)
{
	*w = mwc.w;
	*z = mwc.z;
}

/*
 *  stress_mwc_seed()
 *      set default mwc seed
 */
void stress_mwc_seed(void)
{
	stress_mwc_set_seed(STRESS_MWC_SEED_W, STRESS_MWC_SEED_Z);
}

/*
 *  stress_mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
HOT OPTIMIZE3 uint32_t stress_mwc32(void)
{
	mwc.z = 36969 * (mwc.z & 65535) + (mwc.z >> 16);
	mwc.w = 18000 * (mwc.w & 65535) + (mwc.w >> 16);
	return (mwc.z << 16) + mwc.w;
}

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
	static uint32_t mwc_saved;

	if (LIKELY(mwc_n16)) {
		mwc_n16--;
		mwc_saved >>= 16;
	} else {
		mwc_n16 = 1;
		mwc_saved = stress_mwc32();
	}
	return mwc_saved & 0xffff;
}

/*
 *  stress_mwc8()
 *	get an 8 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t stress_mwc8(void)
{
	static uint32_t mwc_saved;

	if (LIKELY(mwc_n8)) {
		mwc_n8--;
		mwc_saved >>= 8;
	} else {
		mwc_n8 = 3;
		mwc_saved = stress_mwc32();
	}
	return mwc_saved & 0xff;
}

/*
 *  stress_mwc1()
 *	get an 1 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t stress_mwc1(void)
{
	static uint32_t mwc_saved;

	if (LIKELY(mwc_n1)) {
		mwc_n1--;
		mwc_saved >>= 1;
	} else {
		mwc_n1 = 31;
		mwc_saved = stress_mwc32();
	}
	return mwc_saved & 0x1;
}

/*
 *  stress_mwc8modn()
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 *	see https://research.kudelskisecurity.com/2020/07/28/the-definitive-guide-to-modulo-bias-and-how-to-avoid-it/
 */
uint8_t stress_mwc8modn(const uint8_t max)
{
	if (max > 0) {
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
	return 0;
}

/*
 *  stress_mwc16modn()
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 */
uint16_t stress_mwc16modn(const uint16_t max)
{
	if (max > 0) {
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
	return 0;
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 */
uint32_t stress_mwc32modn(const uint32_t max)
{
	if (max > 0) {
		uint32_t threshold = max;
		uint32_t val;

		while (threshold < 0x80000000UL) {
			threshold <<= 1;
		}
		do {
			val = stress_mwc32();
		} while (val >= threshold);

		return val % max;
	}
	return 0;
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 */
uint64_t stress_mwc64modn(const uint64_t max)
{
	if (max > 0) {
		uint64_t threshold = max;
		uint64_t val;

		while (threshold < 0x8000000000000000ULL) {
			threshold <<= 1;
		}
		do {
			val = stress_mwc64();
		} while (val >= threshold);

		return val % max;
	}
	return 0;
}
