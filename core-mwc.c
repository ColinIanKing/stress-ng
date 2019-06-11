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

static mwc_t __mwc = {
	MWC_SEED_W,
	MWC_SEED_Z
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
 *  aux_random_seed()
 *	get a fixed random value via getauxval
 */
static uint64_t aux_random_seed(void)
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
static uint64_t aux_random_seed(void)
{
	return 0ULL;
}
#endif

/*
 *  mwc_reseed()
 *	dirty mwc reseed, this is expensive as it
 *	pulls in various system values for the seeding
 */
void mwc_reseed(void)
{
	if (g_opt_flags & OPT_FLAGS_NO_RAND_SEED) {
		__mwc.w = MWC_SEED_W;
		__mwc.z = MWC_SEED_Z;
	} else {
		struct timeval tv;
		struct rusage r;
		double m1, m5, m15;
		int i, n;
		uint64_t aux_rnd = aux_random_seed();
		const ptrdiff_t p1 = (ptrdiff_t)&__mwc.z;
		const ptrdiff_t p2 = (ptrdiff_t)&tv;

		__mwc.z = aux_rnd >> 32;
		__mwc.w = aux_rnd & 0xffffffff;
		if (gettimeofday(&tv, NULL) == 0)
			__mwc.z ^= (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
		__mwc.z += ~(p1 - p2);
		__mwc.w += (uint64_t)getpid() ^ (uint64_t)getppid()<<12;
		if (stress_get_load_avg(&m1, &m5, &m15) == 0) {
			__mwc.z += (128 * (m1 + m15));
			__mwc.w += (256 * (m5));
		}
		if (getrusage(RUSAGE_SELF, &r) == 0) {
			__mwc.z += r.ru_utime.tv_usec;
			__mwc.w += r.ru_utime.tv_sec;
		}
		__mwc.z ^= stress_get_cpu();
		__mwc.w ^= stress_get_phys_mem_size();

		n = (int)__mwc.z % 1733;
		for (i = 0; i < n; i++) {
			(void)mwc32();
		}
	}
	mwc_flush();
}

/*
 *  mwc_seed()
 *      set mwc seeds
 */
void mwc_seed(const uint32_t w, const uint32_t z)
{
	__mwc.w = w;
	__mwc.z = z;

	mwc_flush();
}

/*
 *  mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
HOT OPTIMIZE3 uint32_t mwc32(void)
{
	__mwc.z = 36969 * (__mwc.z & 65535) + (__mwc.z >> 16);
	__mwc.w = 18000 * (__mwc.w & 65535) + (__mwc.w >> 16);
	return (__mwc.z << 16) + __mwc.w;
}

/*
 *  mwc64()
 *	get a 64 bit pseudo random number
 */
HOT OPTIMIZE3 uint64_t mwc64(void)
{
	return (((uint64_t)mwc32()) << 32) | mwc32();
}

/*
 *  mwc16()
 *	get a 16 bit pseudo random number
 */
HOT OPTIMIZE3 uint16_t mwc16(void)
{
	static uint32_t mwc_saved;

	if (mwc_n16) {
		mwc_n16--;
		mwc_saved >>= 16;
	} else {
		mwc_n16 = 1;
		mwc_saved = mwc32();
	}
	return mwc_saved & 0xffff;
}

/*
 *  mwc8()
 *	get an 8 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t mwc8(void)
{
	static uint32_t mwc_saved;

	if (LIKELY(mwc_n8)) {
		mwc_n8--;
		mwc_saved >>= 8;
	} else {
		mwc_n8 = 3;
		mwc_saved = mwc32();
	}
	return mwc_saved & 0xff;
}

/*
 *  mwc1()
 *	get an 1 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t mwc1(void)
{
	static uint32_t mwc_saved;

	if (LIKELY(mwc_n1)) {
		mwc_n1--;
		mwc_saved >>= 1;
	} else {
		mwc_n8 = 31;
		mwc_saved = mwc32();
	}
	return mwc_saved & 0x1;
}
