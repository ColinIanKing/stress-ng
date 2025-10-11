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
#include "core-attribute.h"
#include "core-bitops.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-helper.h"
#include "core-mwc.h"

#if defined(HAVE_SYS_AUXV_H)
#include <sys/auxv.h>
#endif

#if defined(HAVE_UTIME_H)
#include <utime.h>
#endif

/* MWC random number initial seed */
#define STRESS_MWC_SEED_W	(521288629UL)
#define STRESS_MWC_SEED_Z	(362436069UL)

/* Fast random number generator state */
typedef struct {
	uint32_t w;
	uint32_t z;
	uint32_t n16;
	uint32_t saved16;
	uint32_t n8;
	uint32_t saved8;
	uint32_t n1;
	uint32_t saved1;
} stress_mwc_t;

static stress_mwc_t mwc = {
	STRESS_MWC_SEED_W,
	STRESS_MWC_SEED_Z,
	0,
	0,
	0,
	0,
	0,
	0,
};

/*
 *  mwc_flush()
 *	reset internal mwc cached values, flush out
 */
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
static uint64_t CONST stress_aux_random_seed(void)
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
		union {
			double d_now;
			uint64_t u64_now;
		} u;
		int i, n;
		const uint64_t aux_rnd = stress_aux_random_seed();
		const uint64_t id = stress_get_machine_id();
		const intptr_t p1 = (intptr_t)&mwc;
		const intptr_t p2 = (intptr_t)&tv;

		mwc.z = aux_rnd >> 32;
		mwc.w = aux_rnd & 0xffffffff;
		if (gettimeofday(&tv, NULL) == 0)
			mwc.z ^= (uint64_t)tv.tv_sec ^ (uint64_t)tv.tv_usec;
		mwc.z += ~(p1 - p2);
		mwc.w += shim_rol64n((uint64_t)getpid(), 3) ^ shim_rol64n((uint64_t)getppid(), 1);
		if (stress_get_load_avg(&m1, &m5, &m15) == 0) {
			mwc.z += (uint64_t)(128.0 * (m1 + m15));
			mwc.w += (uint64_t)(256.0 * (m5));
		}
		if (getrusage(RUSAGE_SELF, &r) == 0) {
			mwc.z += r.ru_utime.tv_usec;
			mwc.w += r.ru_utime.tv_sec;
		}

		/*
		 *  Mix in some initial system values
		 */
		mwc.z ^= shim_rol32n(mwc.z, stress_get_cpu() & 0x1f);
		mwc.w ^= shim_rol32n(mwc.w, stress_get_phys_mem_size() >> 22);
		mwc.z ^= stress_get_filesystem_size();
		mwc.z ^= stress_get_kernel_release();
		mwc.w ^= shim_rol32n((uint32_t)stress_get_ticks_per_second(), 3);
		mwc.z ^= shim_ror32n((uint32_t)stress_get_processors_online(), 17);

		mwc.z ^= (uint32_t)(id & 0xffffffffULL);
		mwc.w ^= (uint32_t)((id >> 32) & 0xffffffffULL);

		u.d_now = stress_time_now();
		mwc.z = shim_ror32n(mwc.z, ((u.u64_now >> 4) & 0xf));
		mwc.w = shim_rol32n(mwc.w, (u.u64_now & 0xf));

		n = (int)mwc.z % 1733;
		for (i = 0; i < n; i++) {
			(void)stress_mwc32();
		}

		u.d_now = stress_time_now();
		mwc.z = shim_rol32n(mwc.z, (u.u64_now & 0x7));
		mwc.w = shim_ror32n(mwc.w, ((u.u64_now >> 3) & 0x7));
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
 *  stress_mwc_default_seed()
 *      set default mwc seed
 */
void stress_mwc_default_seed(void)
{
	stress_mwc_set_seed(STRESS_MWC_SEED_W, STRESS_MWC_SEED_Z);
}


/*
 *  stress_mwc32()
 *      Multiply-with-carry random numbers
 *      fast pseudo random number generator, see
 *      http://www.cse.yorku.ca/~oz/marsaglia-rng.html
 */
inline OPTIMIZE3 uint32_t stress_mwc32(void)
{
	mwc.z = 36969 * (mwc.z & 65535) + (mwc.z >> 16);
	mwc.w = 18000 * (mwc.w & 65535) + (mwc.w >> 16);

	return (mwc.z << 16) + mwc.w;
}

/*
 *  stress_mwc64()
 *	get a 64 bit pseudo random number
 */
uint64_t OPTIMIZE3 stress_mwc64(void)
{
	return (((uint64_t)stress_mwc32()) << 32) | stress_mwc32();
}

/*
 *  stress_mwc16()
 *	get a 16 bit pseudo random number
 */
uint16_t OPTIMIZE3 stress_mwc16(void)
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
uint8_t OPTIMIZE3 stress_mwc8(void)
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
uint8_t OPTIMIZE3 stress_mwc1(void)
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

#if !defined(HAVE_FAST_MODULO_REDUCTION)
/*
 *  stress_mwc8mask()
 *	generate a mask large enough for 8 bit val
 */
static inline ALWAYS_INLINE OPTIMIZE3 uint8_t stress_mwc8mask(const uint8_t val)
{
	register uint8_t v = val;

	v |= (v >> 1);
	v |= (v >> 2);
	v |= (v >> 4);
	return v;
}

/*
 *  stress_mwc8modn()
 *	see https://research.kudelskisecurity.com/2020/07/28/the-definitive-guide-to-modulo-bias-and-how-to-avoid-it/
 *	return 8 bit non-modulo biased value 1..max (inclusive)
 */
uint8_t OPTIMIZE3 stress_mwc8modn(const uint8_t max)
{
	register uint8_t mask, val;

	if (UNLIKELY(max < 2))
		return 0;

	mask = stress_mwc8mask(max);
	do {
		val = stress_mwc8() & mask;
	} while (val >= max);

	return val;
}

/*
 *  stress_mwc16mask()
 *	generate a mask large enough for 16 bit val
 */
static inline ALWAYS_INLINE OPTIMIZE3 uint16_t stress_mwc16mask(const uint16_t val)
{
	register uint16_t v = val;

	v |= (v >> 1);
	v |= (v >> 2);
	v |= (v >> 4);
	v |= (v >> 8);
	return v;
}

/*
 *  stress_mwc16modn()
 *	return 16 bit non-modulo biased value 1..max (inclusive)
 *	where max is most probably not a power of 2
 */
uint16_t OPTIMIZE3 stress_mwc16modn(const uint16_t max)
{
	register uint16_t mask, val;

	if (UNLIKELY(max < 2))
		return 0;

	mask = stress_mwc16mask(max);
	do {
		val = stress_mwc16() & mask;
	} while (val >= max);

	return val;
}

/*
 *  stress_mwc32mask()
 *	generate a mask large enough for 32 bit val
 */
static inline ALWAYS_INLINE OPTIMIZE3 uint32_t stress_mwc32mask(const uint32_t val)
{
	register uint32_t v = val;

	v |= (v >> 1);
	v |= (v >> 2);
	v |= (v >> 4);
	v |= (v >> 8);
	v |= (v >> 16);
	return v;
}

/*
 *  stress_mwc32modn()
 *	return 32 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
uint32_t OPTIMIZE3 stress_mwc32modn(const uint32_t max)
{
	register uint32_t mask, val;

	if (UNLIKELY(max < 2))
		return 0;

	mask = stress_mwc32mask(max);
	do {
		val = stress_mwc32() & mask;
	} while (val >= max);

	return val;
}
#endif

#if !defined(HAVE_FAST_MODULO_REDUCTION) ||	\
    !defined(HAVE_INT128_T)
/*
 *  stress_mwc64mask()
 *	generate a mask large enough for 64 bit val
 */
static inline ALWAYS_INLINE OPTIMIZE3 uint64_t stress_mwc64mask(const uint64_t val)
{
	register uint64_t v = val;

	v |= (v >> 1);
	v |= (v >> 2);
	v |= (v >> 4);
	v |= (v >> 8);
	v |= (v >> 16);
	v |= (v >> 32);
	return v;
}

/*
 *  stress_mwc64modn()
 *	return 64 bit non-modulo biased value 1..max (inclusive)
 *	with no non-zero max check
 */
uint64_t OPTIMIZE3 stress_mwc64modn(const uint64_t max)
{
	register uint64_t mask, val;

	if (UNLIKELY(max < 2))
		return 0;

	mask = stress_mwc64mask(max);
	do {
		val = stress_mwc64() & mask;
	} while (val >= max);

	return val;
}
#endif

/*
 *  stress_rndbuf()
 *	fill buffer with pseudorandom bytes
 */
OPTIMIZE3 void stress_rndbuf(void *buf, const size_t len)
{
	register char *ptr = (char *)buf;
	register const char *end = ptr + len;

	while (ptr < end)
		*ptr++ = stress_mwc8();
}

/*
 *  stress_rndstr()
 *	generate pseudorandom string
 */
OPTIMIZE3 void stress_rndstr(char *str, const size_t len)
{
	/*
	 * base64url alphabet.
	 * Be careful if expanding this alphabet, some of this function's users
	 * use it to generate random filenames.
	 */
	static const char NONSTRING alphabet[64] =
		"ABCDEFGHIJKLMNOPQRSTUVWXYZ"
		"abcdefghijklmnopqrstuvwxyz"
		"0123456789-_";
	register uint32_t r, mask;
	register char *ptr, *ptr_end;

	if (len == 0)
		return;

	shim_builtin_prefetch(alphabet);
	ptr = str;
	ptr_end = str + len - 1;
	mask = 0xc0000000;

	r = stress_mwc32() | mask;
	while (LIKELY(ptr < ptr_end)) {
		/* If we don't have enough random bits in r, get more. */
		/*
		 * Use 6 bits from the 32-bit integer at a time.
		 * This means 2 bits from each 32-bit integer are wasted.
		 */
		*(ptr++) = alphabet[r & 0x3F];
		r >>= 6;
		if (r == 0x3)
			r = stress_mwc32() | mask;
	}
	*ptr = '\0';
}

/*
 *  stress_uint8rnd4()
 *	fill a uint8_t buffer full of random data
 *	buffer *must* be multiple of 4 bytes in size
 */
OPTIMIZE3 void stress_uint8rnd4(uint8_t *data, const size_t len)
{
	register uint32_t *ptr32 = (uint32_t *)shim_assume_aligned(data, 4);
	register uint32_t *ptr32end;

	if (UNLIKELY(!data || (len < 4)))
		return;

	ptr32end = (uint32_t *)(data + len);

	if (stress_little_endian()) {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_mwc32();
	} else {
		while (ptr32 < ptr32end)
			*ptr32++ = stress_swap32(stress_mwc32());
	}
}
