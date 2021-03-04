/*
 * Copyright (C) 2013-2021 Canonical, Ltd.
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
	const uint8_t *ptr = (const uint8_t *)getauxval(AT_RANDOM);
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
			mwc.z += (128 * (m1 + m15));
			mwc.w += (256 * (m5));
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
 *  stress_mwc_seed()
 *      set mwc seeds
 */
void stress_mwc_seed(const uint32_t w, const uint32_t z)
{
	mwc.w = w;
	mwc.z = z;

	mwc_flush();
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
