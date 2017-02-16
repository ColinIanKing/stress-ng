/*
 * Copyright (C) 2013-2017 Canonical, Ltd.
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

/*
 *  mwc_reseed()
 *	dirty mwc reseed
 */
void mwc_reseed(void)
{
	if (g_opt_flags & OPT_FLAGS_NO_RAND_SEED) {
		__mwc.w = MWC_SEED_W;
		__mwc.z = MWC_SEED_Z;
	} else {
		struct timeval tv;
		int i, n;

		__mwc.z = 0;
		if (gettimeofday(&tv, NULL) == 0)
			__mwc.z = (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
		__mwc.z += ~((unsigned char *)&__mwc.z - (unsigned char *)&tv);
		__mwc.w = (uint64_t)getpid() ^ (uint64_t)getppid()<<12;

		n = (int)__mwc.z % 1733;
		for (i = 0; i < n; i++) {
			(void)mwc32();
		}
	}
}

/*
 *  mwc_seed()
 *      set mwc seeds
 */
void mwc_seed(const uint32_t w, const uint32_t z)
{
	__mwc.w = w;
	__mwc.z = z;
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
	return mwc32() & 0xffff;
}

/*
 *  mwc8()
 *	get an 8 bit pseudo random number
 */
HOT OPTIMIZE3 uint8_t mwc8(void)
{
	return mwc32() & 0xff;
}
