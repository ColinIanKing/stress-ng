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

/*
 *  For testing, set this to 1 to simulate random memory errors
 */
#define INJECT_BIT_ERRORS	(0)

#define VM_BOGO_SHIFT		(12)
#define VM_ROWHAMMER_LOOPS	(1000000)

#define NO_MEM_RETRIES_MAX	(100)

/*
 *  the VM stress test has diffent methods of vm stressor
 */
typedef size_t (*stress_vm_func)(uint8_t *buf, const size_t sz,
		uint64_t *counter, const uint64_t max_ops);

typedef struct {
	const char *name;
	const stress_vm_func func;
} stress_vm_stressor_info_t;

static uint64_t opt_vm_hang = DEFAULT_VM_HANG;
static size_t   opt_vm_bytes = DEFAULT_VM_BYTES;
static bool	set_vm_bytes = false;
static int      opt_vm_flags = 0;                      /* VM mmap flags */

static const stress_vm_stressor_info_t *opt_vm_stressor;
static const stress_vm_stressor_info_t vm_methods[];

void stress_set_vm_hang(const char *optarg)
{
	opt_vm_hang = get_uint64_byte(optarg);
	check_range("vm-hang", opt_vm_hang,
		MIN_VM_HANG, MAX_VM_HANG);
}

void stress_set_vm_bytes(const char *optarg)
{
	set_vm_bytes = true;
	opt_vm_bytes = (size_t)
		get_uint64_byte_memory(optarg,
			stressor_instances(STRESS_VM));
	check_range_bytes("vm-bytes", opt_vm_bytes,
		MIN_VM_BYTES, MAX_MEM_LIMIT);
}

void stress_set_vm_flags(const int flag)
{
	opt_vm_flags |= flag;
}

#define SET_AND_TEST(ptr, val, bit_errors)	\
{						\
	*ptr = val;				\
	bit_errors += (*ptr != val);		\
}

/*
 *  This compiles down to a load, ror, store in x86
 */
#define ROR64(val) 				\
{						\
	uint64_t tmp = val;			\
	uint64_t bit0 = (tmp & 1) << 63; 	\
	tmp >>= 1;				\
	tmp |= bit0;				\
	val = tmp;				\
}

#define ROR8(val) 				\
{						\
	uint8_t tmp = val;			\
	uint8_t bit0 = (tmp & 1) << 7; 		\
	tmp >>= 1;				\
	tmp |= bit0;				\
	val = tmp;				\
}

#define INC_LO_NYBBLE(val)			\
{						\
	uint8_t lo = (val);			\
	lo += 1;				\
	lo &= 0xf;				\
	(val) = ((val) & 0xf0) | lo;		\
}

#define INC_HI_NYBBLE(val)			\
{						\
	uint8_t hi = (val);			\
	hi += 0xf0;				\
	hi &= 0xf0;				\
	(val) = ((val) & 0x0f) | hi;		\
}

#define UNSIGNED_ABS(a, b)			\
	((a) > (b)) ? (a) - (b) : (b) - (a)

#if INJECT_BIT_ERRORS
/*
 *  inject_random_bit_errors()
 *	for testing purposes, we can insert various faults
 */
static void inject_random_bit_errors(uint8_t *buf, const size_t sz)
{
	int i;

	for (i = 0; i < 8; i++) {
		/* 1 bit errors */
		buf[random() % sz] ^= (1 << i);
		buf[random() % sz] |= (1 << i);
		buf[random() % sz] &= ~(1 << i);
	}

	for (i = 0; i < 7; i++) {
		/* 2 bit errors */
		buf[random() % sz] ^= (3 << i);
		buf[random() % sz] |= (3 << i);
		buf[random() % sz] &= ~(3 << i);
	}

	for (i = 0; i < 6; i++) {
		/* 3 bit errors */
		buf[random() % sz] ^= (7 << i);
		buf[random() % sz] |= (7 << i);
		buf[random() % sz] &= ~(7 << i);
	}
}
#else
/* No-op */
static inline void inject_random_bit_errors(uint8_t *buf, const size_t sz)
{
	(void)buf;
	(void)sz;
}
#endif


/*
 *  stress_vm_check()
 *	report back on bit errors found
 */
static void stress_vm_check(const char *name, const size_t bit_errors)
{
	if (bit_errors && (g_opt_flags & OPT_FLAGS_VERIFY))
#if INJECT_BIT_ERRORS
		pr_dbg("%s: detected %zu memory error%s\n",
			name, bit_errors, bit_errors == 1 ? "" : "s");
#else
		pr_fail("%s: detected %zu memory error%s\n",
			name, bit_errors, bit_errors == 1 ? "" : "s");
#endif
}

/*
 *  stress_vm_count_bits()
 *	count number of bits set (K and R)
 */
static inline size_t stress_vm_count_bits(uint64_t v)
{
	size_t n;

	for (n = 0; v; n++)
		v &= v - 1;

	return n;
}

/*
 *  stress_vm_moving_inversion()
 *	work sequentially through memory setting 8 bytes at at a time
 *	with a random value, then check if it is correct, invert it and
 *	then check if that is correct.
 */
static size_t stress_vm_moving_inversion(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	uint64_t w, z, *buf_end, c = *counter;
	volatile uint64_t *ptr;
	size_t bit_errors;

	buf_end = (uint64_t *)(buf + sz);

	mwc_reseed();
	w = mwc64();
	z = mwc64();

	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < buf_end; ) {
		*(ptr++) = mwc64();
	}

	mwc_seed(w, z);
	for (bit_errors = 0, ptr = (uint64_t *)buf; ptr < buf_end; ) {
		uint64_t val = mwc64();

		if (*ptr != val)
			bit_errors++;
		*(ptr++) = ~val;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	(void)mincore_touch_pages(buf, sz);

	inject_random_bit_errors(buf, sz);

	mwc_seed(w, z);
	for (bit_errors = 0, ptr = (uint64_t *)buf; ptr < buf_end; ) {
		uint64_t val = mwc64();
		if (*(ptr++) != ~val)
			bit_errors++;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		*--ptr = mwc64();
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
	}

	inject_random_bit_errors(buf, sz);

	(void)mincore_touch_pages(buf, sz);
	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		uint64_t val = mwc64();
		if (*--ptr != val)
			bit_errors++;
		*ptr = ~val;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		uint64_t val = mwc64();
		if (*--ptr != ~val)
			bit_errors++;
		if (!g_keep_stressing_flag)
			goto abort;
	}

abort:
	stress_vm_check("moving inversion", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_modulo_x()
 *	set every 23rd byte to a random pattern and then set
 *	all the other bytes to the complement of this. Check
 *	that the random patterns are still set.
 */
static size_t stress_vm_modulo_x(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	uint32_t i, j;
	const uint32_t stride = 23;	/* Small prime to hit cache */
	uint8_t pattern, compliment;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	size_t bit_errors = 0;
	uint64_t c = *counter;

	mwc_reseed();
	pattern = mwc8();
	compliment = ~pattern;

	for (i = 0; i < stride; i++) {
		for (ptr = buf + i; ptr < buf_end; ptr += stride) {
			*ptr = pattern;
			if (!g_keep_stressing_flag)
				goto abort;
		}
		for (ptr = buf; ptr < buf_end; ptr += stride) {
			for (j = 0; j < i && ptr < buf_end; j++) {
				*ptr++ = compliment;
				c++;
				if (max_ops && c >= max_ops)
					goto abort;
			}
			if (!g_keep_stressing_flag)
				goto abort;
			ptr++;
			for (j = i + 1; j < stride && ptr < buf_end; j++) {
				*ptr++ = compliment;
				c++;
				if (max_ops && c >= max_ops)
					goto abort;
			}
			if (!g_keep_stressing_flag)
				goto abort;
		}
		inject_random_bit_errors(buf, sz);

		for (ptr = buf + i; ptr < buf_end; ptr += stride) {
			if (*ptr != pattern)
				bit_errors++;
			if (!g_keep_stressing_flag)
				return bit_errors;
		}
	}

abort:
	stress_vm_check("modulo X", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_walking_one_data()
 *	for each byte, walk through each data line setting them to high
 *	setting each bit to see if none of the lines are stuck
 */
static size_t stress_vm_walking_one_data(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint64_t c = *counter;

	for (ptr = buf; ptr < buf_end; ptr++) {
		SET_AND_TEST(ptr, 0x01, bit_errors);
		SET_AND_TEST(ptr, 0x02, bit_errors);
		SET_AND_TEST(ptr, 0x04, bit_errors);
		SET_AND_TEST(ptr, 0x08, bit_errors);
		SET_AND_TEST(ptr, 0x10, bit_errors);
		SET_AND_TEST(ptr, 0x20, bit_errors);
		SET_AND_TEST(ptr, 0x40, bit_errors);
		SET_AND_TEST(ptr, 0x80, bit_errors);
		c++;
		if (max_ops && c >= max_ops)
			break;
		if (!g_keep_stressing_flag)
			break;
	}
	stress_vm_check("walking one (data)", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_walking_zero_data()
 *	for each byte, walk through each data line setting them to low
 *	setting each bit to see if none of the lines are stuck
 */
static size_t stress_vm_walking_zero_data(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint64_t c = *counter;

	for (ptr = buf; ptr < buf_end; ptr++) {
		SET_AND_TEST(ptr, 0xfe, bit_errors);
		SET_AND_TEST(ptr, 0xfd, bit_errors);
		SET_AND_TEST(ptr, 0xfb, bit_errors);
		SET_AND_TEST(ptr, 0xf7, bit_errors);
		SET_AND_TEST(ptr, 0xef, bit_errors);
		SET_AND_TEST(ptr, 0xdf, bit_errors);
		SET_AND_TEST(ptr, 0xbf, bit_errors);
		SET_AND_TEST(ptr, 0x7f, bit_errors);
		c++;
		if (max_ops && c >= max_ops)
			break;
		if (!g_keep_stressing_flag)
			break;
	}
	stress_vm_check("walking zero (data)", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_walking_one_addr()
 *	work through a range of addresses setting each address bit in
 *	the given memory mapped range to high to see if any address bits
 *	are stuck.
 */
static size_t stress_vm_walking_one_addr(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint8_t d1 = 0, d2 = ~d1;
	size_t bit_errors = 0;
	size_t tests = 0;
	uint64_t c = *counter;

	memset(buf, d1, sz);
	for (ptr = buf; ptr < buf_end; ptr += 256) {
		uint16_t i;
		uint64_t mask;

		*ptr = d1;
		for (mask = 1, i = 1; i < 64; i++) {
			uintptr_t uintptr = ((uintptr_t)ptr) ^ mask;
			uint8_t *addr = (uint8_t *)uintptr;
			if (addr == ptr)
				continue;
			if (addr < buf || addr >= buf_end || addr == ptr)
				continue;
			*addr = d2;
			tests++;
			if (*ptr != d1)
				bit_errors++;
			mask <<= 1;
		}
		c++;
		if (max_ops && c >= max_ops)
			break;
		if (!g_keep_stressing_flag)
			break;
	}
	stress_vm_check("walking one (address)", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_walking_zero_addr()
 *	work through a range of addresses setting each address bit in
 *	the given memory mapped range to low to see if any address bits
 *	are stuck.
 */
static size_t stress_vm_walking_zero_addr(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint8_t d1 = 0, d2 = ~d1;
	size_t bit_errors = 0;
	size_t tests = 0;
	uint64_t sz_mask;
	uint64_t c = *counter;

	for (sz_mask = 1; sz_mask < sz; sz_mask <<= 1)
		;

	sz_mask--;

	memset(buf, d1, sz);
	for (ptr = buf; ptr < buf_end; ptr += 256) {
		uint16_t i;
		uint64_t mask;

		*ptr = d1;
		for (mask = 1, i = 1; i < 64; i++) {
			uintptr_t uintptr = ((uintptr_t)ptr) ^ (~mask & sz_mask);
			uint8_t *addr = (uint8_t *)uintptr;
			if (addr < buf || addr >= buf_end || addr == ptr)
				continue;
			*addr = d2;
			tests++;
			if (*ptr != d1)
				bit_errors++;
			mask <<= 1;
		}
		c++;
		if (max_ops && c >= max_ops)
			break;
		if (!g_keep_stressing_flag)
			break;
	}
	stress_vm_check("walking zero (address)", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_gray()
 *	fill all of memory with a gray code and check that
 *	all the bits are set correctly. gray codes just change
 *	one bit at a time.
 */
static size_t stress_vm_gray(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static uint8_t val;
	uint8_t v, *buf_end = buf + sz;
	volatile uint8_t *ptr;
	size_t bit_errors = 0;
	uint64_t c = *counter;

	for (v = val, ptr = buf; ptr < buf_end; ptr++, v++) {
		if (!g_keep_stressing_flag)
			return 0;
		*ptr = (v >> 1) ^ v;
		c++;
		if (max_ops && c >= max_ops)
			break;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (v = val, ptr = buf; ptr < buf_end; ptr++, v++) {
		if (!g_keep_stressing_flag)
			break;
		if (*ptr != ((v >> 1) ^ v))
			bit_errors++;
	}
	val++;

	stress_vm_check("gray code", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_incdec()
 *	work through memory incrementing it and then decrementing
 *	it by a value that changes on each test iteration.
 *	Check that the memory has not changed by the inc + dec
 *	operations.
 */
static size_t stress_vm_incdec(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	uint8_t *buf_end = buf + sz;
	volatile uint8_t *ptr;
	size_t bit_errors = 0;
	uint64_t c = *counter;

	val++;
	memset(buf, 0x00, sz);

	for (ptr = buf; ptr < buf_end; ptr++) {
		*ptr += val;
		c++;
		if (max_ops && c >= max_ops)
			break;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	for (ptr = buf; ptr < buf_end; ptr++) {
		*ptr -= val;
		c++;
		if (max_ops && c >= max_ops)
			break;
	}

	for (ptr = buf; ptr < buf_end; ptr++) {
		if (*ptr != 0)
			bit_errors++;
	}

	stress_vm_check("incdec code", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_prime_incdec()
 *	walk through memory in large prime steps incrementing
 *	bytes and then re-walk again decrementing; then sanity
 *	check.
 */
static size_t stress_vm_prime_incdec(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	uint8_t *buf_end = buf + sz;
	volatile uint8_t *ptr = buf;
	size_t bit_errors = 0, i;
	const uint64_t prime = PRIME_64;
	uint64_t j, c = *counter;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (sz > (1ULL << 63))
		return 0;
#endif

	memset(buf, 0x00, sz);

	for (i = 0; i < sz; i++) {
		ptr[i] += val;
		c++;
		if (max_ops && c >= max_ops)
			break;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	for (i = 0, j = prime; i < sz; i++, j += prime) {
		ptr[j % sz] -= val;
		c++;
		if (max_ops && c >= max_ops)
			break;
	}

	for (ptr = buf; ptr < buf_end; ptr++) {
		if (*ptr != 0)
			bit_errors++;
	}

	stress_vm_check("prime-incdec", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_swap()
 *	forward swap and then reverse swap chunks of memory
 *	and see that nothing got corrupted.
 */
static size_t stress_vm_swap(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	const size_t chunk_sz = 64, chunks = sz / chunk_sz;
	uint64_t w1, z1, c = *counter;
	uint8_t *buf_end = buf + sz;
	uint8_t *ptr;
	size_t bit_errors = 0, i;
	size_t *swaps;

	mwc_reseed();
	z1 = mwc64();
	w1 = mwc64();

	if ((swaps = calloc(chunks, sizeof(size_t))) == NULL) {
		pr_fail("stress-vm: calloc failed on vm_swap\n");
		return 0;
	}

	for (i = 0; i < chunks; i++) {
		swaps[i] = (mwc64() % chunks) * chunk_sz;
	}

	mwc_seed(w1, z1);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();
		memset((void *)ptr, val, chunk_sz);
	}

	/* Forward swaps */
	for (i = 0, ptr = buf; ptr < buf_end; ptr += chunk_sz, i++) {
		size_t offset = swaps[i];

		volatile uint8_t *dst = buf + offset;
		volatile uint8_t *src = (volatile uint8_t *)ptr;
		volatile uint8_t *src_end = src + chunk_sz;

		while (src < src_end) {
			uint8_t tmp = *src;
			*src++ = *dst;
			*dst++ = tmp;
		}
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	/* Reverse swaps */
	for (i = chunks - 1, ptr = buf_end - chunk_sz; ptr >= buf; ptr -= chunk_sz, i--) {
		size_t offset = swaps[i];

		volatile uint8_t *dst = buf + offset;
		volatile uint8_t *src = (volatile uint8_t *)ptr;
		volatile uint8_t *src_end = src + chunk_sz;

		while (src < src_end) {
			uint8_t tmp = *src;
			*src++ = *dst;
			*dst++ = tmp;
		}
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	mwc_seed(w1, z1);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		volatile uint8_t *p = (volatile uint8_t *)ptr;
		volatile uint8_t *p_end = (volatile uint8_t *)ptr + chunk_sz;
		uint8_t val = mwc8();

		while (p < p_end) {
			if (*p != val)
				bit_errors++;
			p++;
		}
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	free(swaps);
	stress_vm_check("swap bytes", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_rand_set()
 *	fill 64 bit chunks of memory with a random pattern and
 *	and then sanity check they are all set correctly.
 */
static size_t stress_vm_rand_set(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	const size_t chunk_sz = sizeof(uint8_t) * 8;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint64_t w, z, c = *counter;
	size_t bit_errors = 0;

	mwc_reseed();
	w = mwc64();
	z = mwc64();

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();

		*(ptr + 0) = val;
		*(ptr + 1) = val;
		*(ptr + 2) = val;
		*(ptr + 3) = val;
		*(ptr + 4) = val;
		*(ptr + 5) = val;
		*(ptr + 6) = val;
		*(ptr + 7) = val;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();

		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("rand-set", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_ror()
 *	fill memory with a random pattern and then rotate
 *	right all the bits in an 8 byte (64 bit) chunk
 *	and then sanity check they are all shifted at the
 *	end.
 */
static size_t stress_vm_ror(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	const size_t chunk_sz = sizeof(uint8_t) * 8;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	uint64_t w, z, c = *counter;
	size_t bit_errors = 0;

	mwc_reseed();
	w = mwc64();
	z = mwc64();

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();

		*(ptr + 0) = val;
		*(ptr + 1) = val;
		*(ptr + 2) = val;
		*(ptr + 3) = val;
		*(ptr + 4) = val;
		*(ptr + 5) = val;
		*(ptr + 6) = val;
		*(ptr + 7) = val;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);

	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		ROR64(*(ptr + 0));
		ROR64(*(ptr + 1));
		ROR64(*(ptr + 2));
		ROR64(*(ptr + 3));
		ROR64(*(ptr + 4));
		ROR64(*(ptr + 5));
		ROR64(*(ptr + 6));
		ROR64(*(ptr + 7));
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);

	inject_random_bit_errors(buf, sz);

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();
		ROR64(val);

		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("ror", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_flip()
 *	set all memory to random pattern, then work through
 *	memory 8 times flipping bits 0..7 on by one to eventually
 *	invert all the bits.  Check if the final bits are all
 *	correctly inverted.
 */
static size_t stress_vm_flip(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	const size_t chunk_sz = sizeof(uint8_t) * 8;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz, bit = 0x03;
	uint64_t w, z, c = *counter;
	size_t bit_errors = 0, i;

	mwc_reseed();
	w = mwc64();
	z = mwc64();

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();

		*(ptr + 0) = val;
		ROR8(val);
		*(ptr + 1) = val;
		ROR8(val);
		*(ptr + 2) = val;
		ROR8(val);
		*(ptr + 3) = val;
		ROR8(val);
		*(ptr + 4) = val;
		ROR8(val);
		*(ptr + 5) = val;
		ROR8(val);
		*(ptr + 6) = val;
		ROR8(val);
		*(ptr + 7) = val;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);

	for (i = 0; i < 8; i++) {
		ROR8(bit);
		for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
			*(ptr + 0) ^= bit;
			*(ptr + 1) ^= bit;
			*(ptr + 2) ^= bit;
			*(ptr + 3) ^= bit;
			*(ptr + 4) ^= bit;
			*(ptr + 5) ^= bit;
			*(ptr + 6) ^= bit;
			*(ptr + 7) ^= bit;
			c++;
			if (max_ops && c >= max_ops)
				goto abort;
			if (!g_keep_stressing_flag)
				goto abort;
		}
		(void)mincore_touch_pages(buf, sz);
	}

	inject_random_bit_errors(buf, sz);

	mwc_seed(w, z);
	for (ptr = buf; ptr < buf_end; ptr += chunk_sz) {
		uint8_t val = mwc8();

		bit_errors += (*(ptr + 0) != val);
		ROR8(val);
		bit_errors += (*(ptr + 1) != val);
		ROR8(val);
		bit_errors += (*(ptr + 2) != val);
		ROR8(val);
		bit_errors += (*(ptr + 3) != val);
		ROR8(val);
		bit_errors += (*(ptr + 4) != val);
		ROR8(val);
		bit_errors += (*(ptr + 5) != val);
		ROR8(val);
		bit_errors += (*(ptr + 6) != val);
		ROR8(val);
		bit_errors += (*(ptr + 7) != val);
		if (!g_keep_stressing_flag)
			break;
	}

abort:
	stress_vm_check("flip", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_zero_one()
 *	set all memory to zero and see if any bits are stuck at one and
 *	set all memory to one and see if any bits are stuck at zero
 */
static size_t stress_vm_zero_one(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint64_t *ptr;
	uint64_t *buf_end = (uint64_t *)(buf + sz);
	uint64_t c = *counter;
	size_t bit_errors = 0;

	(void)max_ops;

	memset(buf, 0x00, sz);
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(*(ptr + 0));
		bit_errors += stress_vm_count_bits(*(ptr + 1));
		bit_errors += stress_vm_count_bits(*(ptr + 2));
		bit_errors += stress_vm_count_bits(*(ptr + 3));
		bit_errors += stress_vm_count_bits(*(ptr + 4));
		bit_errors += stress_vm_count_bits(*(ptr + 5));
		bit_errors += stress_vm_count_bits(*(ptr + 6));
		bit_errors += stress_vm_count_bits(*(ptr + 7));

		if (!g_keep_stressing_flag)
			goto abort;
	}

	memset(buf, 0xff, sz);
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(~*(ptr + 0));
		bit_errors += stress_vm_count_bits(~*(ptr + 1));
		bit_errors += stress_vm_count_bits(~*(ptr + 2));
		bit_errors += stress_vm_count_bits(~*(ptr + 3));
		bit_errors += stress_vm_count_bits(~*(ptr + 4));
		bit_errors += stress_vm_count_bits(~*(ptr + 5));
		bit_errors += stress_vm_count_bits(~*(ptr + 6));
		bit_errors += stress_vm_count_bits(~*(ptr + 7));

		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("zero-one", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_galpat_one()
 *	galloping pattern. Set all bits to zero and flip a few
 *	random bits to one.  Check if this one is pulled down
 *	or pulls its neighbours up.
 */
static size_t stress_vm_galpat_zero(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint64_t *ptr;
	uint64_t *buf_end = (uint64_t *)(buf + sz);
	size_t i, bit_errors = 0, bits_set = 0;
	size_t bits_bad = sz / 4096;
	uint64_t c = *counter;

	memset(buf, 0x00, sz);

	mwc_reseed();

	for (i = 0; i < bits_bad; i++) {
		for (;;) {
			size_t offset = mwc64() % sz;
			uint8_t bit = mwc32() & 3;

			if (!buf[offset]) {
				buf[offset] |= (1 << bit);
				break;
			}
		}
		c++;
		if (max_ops && c >= max_ops)
			break;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += 8) {
		bits_set += stress_vm_count_bits(*(ptr + 0));
		bits_set += stress_vm_count_bits(*(ptr + 1));
		bits_set += stress_vm_count_bits(*(ptr + 2));
		bits_set += stress_vm_count_bits(*(ptr + 3));
		bits_set += stress_vm_count_bits(*(ptr + 4));
		bits_set += stress_vm_count_bits(*(ptr + 5));
		bits_set += stress_vm_count_bits(*(ptr + 6));
		bits_set += stress_vm_count_bits(*(ptr + 7));

		if (!g_keep_stressing_flag)
			break;
	}

	if (bits_set != bits_bad)
		bit_errors += UNSIGNED_ABS(bits_set, bits_bad);

	stress_vm_check("galpat-zero", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_galpat_one()
 *	galloping pattern. Set all bits to one and flip a few
 *	random bits to zero.  Check if this zero is pulled up
 *	or pulls its neighbours down.
 */
static size_t stress_vm_galpat_one(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint64_t *ptr;
	uint64_t *buf_end = (uint64_t *)(buf + sz);
	size_t i, bit_errors = 0, bits_set = 0;
	size_t bits_bad = sz / 4096;
	uint64_t c = *counter;

	memset(buf, 0xff, sz);

	mwc_reseed();

	for (i = 0; i < bits_bad; i++) {
		for (;;) {
			size_t offset = mwc64() % sz;
			uint8_t bit = mwc32() & 3;

			if (buf[offset] == 0xff) {
				buf[offset] &= ~(1 << bit);
				break;
			}
		}
		c++;
		if (max_ops && c >= max_ops)
			break;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += 8) {
		bits_set += stress_vm_count_bits(~(*(ptr + 0)));
		bits_set += stress_vm_count_bits(~(*(ptr + 1)));
		bits_set += stress_vm_count_bits(~(*(ptr + 2)));
		bits_set += stress_vm_count_bits(~(*(ptr + 3)));
		bits_set += stress_vm_count_bits(~(*(ptr + 4)));
		bits_set += stress_vm_count_bits(~(*(ptr + 5)));
		bits_set += stress_vm_count_bits(~(*(ptr + 6)));
		bits_set += stress_vm_count_bits(~(*(ptr + 7)));
		if (!g_keep_stressing_flag)
			break;
	}

	if (bits_set != bits_bad)
		bit_errors += UNSIGNED_ABS(bits_set, bits_bad);

	stress_vm_check("galpat-one", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_inc_nybble()
 *	work through memort and bump increment lower nybbles by
 *	1 and upper nybbles by 0xf and sanity check byte.
 */
static size_t stress_vm_inc_nybble(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	volatile uint8_t *ptr;
	uint8_t *buf_end = buf + sz;
	size_t bit_errors = 0;
	uint64_t c = *counter;

	memset(buf, val, sz);
	INC_LO_NYBBLE(val);
	INC_HI_NYBBLE(val);

	mwc_reseed();
	for (ptr = buf; ptr < buf_end; ptr += 8) {
		INC_LO_NYBBLE(*(ptr + 0));
		INC_LO_NYBBLE(*(ptr + 1));
		INC_LO_NYBBLE(*(ptr + 2));
		INC_LO_NYBBLE(*(ptr + 3));
		INC_LO_NYBBLE(*(ptr + 4));
		INC_LO_NYBBLE(*(ptr + 5));
		INC_LO_NYBBLE(*(ptr + 6));
		INC_LO_NYBBLE(*(ptr + 7));
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	for (ptr = buf; ptr < buf_end; ptr += 8) {
		INC_HI_NYBBLE(*(ptr + 0));
		INC_HI_NYBBLE(*(ptr + 1));
		INC_HI_NYBBLE(*(ptr + 2));
		INC_HI_NYBBLE(*(ptr + 3));
		INC_HI_NYBBLE(*(ptr + 4));
		INC_HI_NYBBLE(*(ptr + 5));
		INC_HI_NYBBLE(*(ptr + 6));
		INC_HI_NYBBLE(*(ptr + 7));
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = buf; ptr < buf_end; ptr += 8) {
		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (!g_keep_stressing_flag)
			break;
	}

abort:
	stress_vm_check("inc-nybble", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_rand_sum()
 *	sequentially set all memory to random values and then
 *	check if they are still set correctly.
 */
static size_t stress_vm_rand_sum(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	const size_t chunk_sz = sizeof(uint8_t) * 8;
	volatile uint64_t *ptr;
	uint64_t *buf_end = (uint64_t *)(buf + sz);
	uint64_t w, z, c = *counter;
	size_t bit_errors = 0;

	mwc_reseed();
	w = mwc64();
	z = mwc64();

	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += chunk_sz) {
		*(ptr + 0) = mwc64();
		*(ptr + 1) = mwc64();
		*(ptr + 2) = mwc64();
		*(ptr + 3) = mwc64();
		*(ptr + 4) = mwc64();
		*(ptr + 5) = mwc64();
		*(ptr + 6) = mwc64();
		*(ptr + 7) = mwc64();
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
		if (!g_keep_stressing_flag)
			goto abort;
	}

	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	mwc_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < buf_end; ptr += chunk_sz) {
		bit_errors += stress_vm_count_bits(*(ptr + 0) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 1) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 2) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 3) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 4) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 5) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 6) ^ mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 7) ^ mwc64());
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("rand-sum", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_prime_zero()
 *	step through memory in non-contiguous large steps
 *	and clearing each bit to one (one bit per complete memory cycle)
 *	and check if they are clear.
 */
static size_t stress_vm_prime_zero(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t i;
	volatile uint8_t *ptr = buf;
	uint8_t j;
	size_t bit_errors = 0;
	const uint64_t prime = PRIME_64;
	uint64_t k, c = *counter;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (sz > (1ULL << 63))
		return 0;
#endif

	memset(buf, 0xff, sz);

	for (j = 0; j < 8; j++) {
		uint8_t mask = ~(1 << j);
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		for (i = 0, k = prime; i < sz; i++, k += prime) {
			ptr[k % sz] &= mask;
			c++;
			if (max_ops && c >= max_ops)
				goto abort;
			if (!g_keep_stressing_flag)
				goto abort;
		}
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (i = 0; i < sz; i++) {
		bit_errors += stress_vm_count_bits(buf[i]);
	}

abort:
	stress_vm_check("prime-zero", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_prime_one()
 *	step through memory in non-contiguous large steps
 *	and set each bit to one (one bit per complete memory cycle)
 *	and check if they are set.
 */
static size_t stress_vm_prime_one(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t i;
	volatile uint8_t *ptr = buf;
	uint8_t j;
	size_t bit_errors = 0;
	const uint64_t prime = PRIME_64;
	uint64_t k, c = *counter;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (sz > (1ULL << 63))
		return 0;
#endif

	memset(buf, 0x00, sz);

	for (j = 0; j < 8; j++) {
		uint8_t mask = 1 << j;
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		for (i = 0, k = prime; i < sz; i++, k += prime) {
			ptr[k % sz] |= mask;
			c++;
			if (max_ops && c >= max_ops)
				goto abort;
			if (!g_keep_stressing_flag)
				goto abort;
		}
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (i = 0; i < sz; i++) {
		bit_errors += 8 - stress_vm_count_bits(buf[i]);
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("prime-one", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_prime_gray_zero()
 *	step through memory in non-contiguous large steps
 *	and first clear just one bit (based on gray code) and then
 *	clear all the other bits and finally check if thay are all clear
 */
static size_t stress_vm_prime_gray_zero(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t i;
	volatile uint8_t *ptr = buf;
	size_t bit_errors = 0;
	const uint64_t prime = PRIME_64;
	uint64_t j, c = *counter;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (sz > (1ULL << 63))
		return 0;
#endif

	memset(buf, 0xff, sz);

	for (i = 0, j = prime; i < sz; i++, j += prime) {
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		ptr[j % sz] &= ((i >> 1) ^ i);
		if (!g_keep_stressing_flag)
			goto abort;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
	}
	for (i = 0, j = prime; i < sz; i++, j += prime) {
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		ptr[j % sz] &= ~((i >> 1) ^ i);
		if (!g_keep_stressing_flag)
			goto abort;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (i = 0; i < sz; i++) {
		bit_errors += stress_vm_count_bits(buf[i]);
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("prime-gray-zero", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_prime_one()
 *	step through memory in non-contiguous large steps
 *	and first set just one bit (based on gray code) and then
 *	set all the other bits and finally check if thay are all set
 */
static size_t stress_vm_prime_gray_one(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t i;
	volatile uint8_t *ptr = buf;
	size_t bit_errors = 0;
	const uint64_t prime = PRIME_64;
	uint64_t j, c = *counter;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (sz > (1ULL << 63))
		return 0;
#endif

	memset(buf, 0x00, sz);

	for (i = 0, j = prime; i < sz; i++, j += prime) {
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		ptr[j % sz] |= ((i >> 1) ^ i);
		if (!g_keep_stressing_flag)
			goto abort;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);
	for (i = 0, j = prime; i < sz; i++, j += prime) {
		/*
		 *  Step through memory in prime sized steps
		 *  in a totally sub-optimal way to exercise
		 *  memory and cache stalls
		 */
		ptr[j % sz] |= ~((i >> 1) ^ i);
		if (!g_keep_stressing_flag)
			goto abort;
		c++;
		if (max_ops && c >= max_ops)
			goto abort;
	}
	(void)mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (i = 0; i < sz; i++) {
		bit_errors += 8 - stress_vm_count_bits(buf[i]);
		if (!g_keep_stressing_flag)
			break;
	}
abort:
	stress_vm_check("prime-gray-one", bit_errors);
	*counter = c;

	return bit_errors;
}

/*
 *  stress_vm_write_64()
 *	simple 64 bit write, no read check
 */
static size_t stress_vm_write64(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static uint64_t val;
	uint64_t *ptr = (uint64_t *)buf;
	register uint64_t v = val;
	register size_t i = 0, n = sz / (sizeof(uint64_t) * 32);

	while (i < n) {
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;

		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;

		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;

		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		*ptr++ = v;
		i++;
		if (!g_keep_stressing_flag || (max_ops && i >= max_ops))
			break;
	}
	*counter += i;
	val++;

	return 0;
}

/*
 *  stress_vm_read_64()
 *	simple 64 bit read
 */
static size_t stress_vm_read64(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	volatile uint64_t *ptr = (uint64_t *)buf;
	register size_t i = 0, n = sz / (sizeof(uint64_t) * 32);

	while (i < n) {
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);

		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);

		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);

		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);
		(void)*(ptr++);

		i++;
		if (!g_keep_stressing_flag || (max_ops && i >= max_ops))
			break;
	}
	*counter += i;

	return 0;
}

/*
 *  stress_vm_rowhammer()
 *
 */
static size_t stress_vm_rowhammer(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	uint32_t *buf32 = (uint32_t *)buf;
	static uint32_t val = 0xff5a00a5;
	const size_t n = sz / sizeof(uint32_t);
	register size_t j;

	(void)max_ops;

	if (!n) {
		pr_dbg("rowhammer: zero uint32_t integers could "
			"be hammered, aborting\n");
		return 0;
	}

	(void)mincore_touch_pages(buf, sz);

	for (j = 0; j < n; j++)
		buf32[j] = val;
	register volatile uint32_t *addr0, *addr1;
	register size_t errors = 0;

	/* Pick two random addresses */
	addr0 = &buf32[(mwc64() << 12) % n];
	addr1 = &buf32[(mwc64() << 12) % n];

	/* Hammer the rows */
	for (j = VM_ROWHAMMER_LOOPS / 4; j; j--) {
		*addr0;
		*addr1;
		clflush(addr0);
		clflush(addr1);
		*addr0;
		*addr1;
		clflush(addr0);
		clflush(addr1);
		*addr0;
		*addr1;
		clflush(addr0);
		clflush(addr1);
		*addr0;
		*addr1;
		clflush(addr0);
		clflush(addr1);
	}
	for (j = 0; j < n; j++)
		if (buf32[j] != val)
			errors++;
	if (errors) {
		bit_errors += errors;
		pr_dbg("rowhammer: %zu errors on addresses "
			"%p and %p\n", errors, addr0, addr1);
	}
	(*counter) += VM_ROWHAMMER_LOOPS;
	val = (val >> 31) | (val << 1);

	stress_vm_check("rowhammer", bit_errors);

	return bit_errors;
}

/*
 *  stress_vm_all()
 *	work through all vm stressors sequentially
 */
static size_t stress_vm_all(
	uint8_t *buf,
	const size_t sz,
	uint64_t *counter,
	const uint64_t max_ops)
{
	static int i = 1;
	size_t bit_errors = 0;

	bit_errors = vm_methods[i].func(buf, sz, counter, max_ops);
	i++;
	if (vm_methods[i].func == NULL)
		i = 1;

	return bit_errors;
}

static const stress_vm_stressor_info_t vm_methods[] = {
	{ "all",	stress_vm_all },
	{ "flip",	stress_vm_flip },
	{ "galpat-0",	stress_vm_galpat_zero },
	{ "galpat-1",	stress_vm_galpat_one },
	{ "gray",	stress_vm_gray },
	{ "rowhammer",	stress_vm_rowhammer },
	{ "incdec",	stress_vm_incdec },
	{ "inc-nybble",	stress_vm_inc_nybble },
	{ "rand-set",	stress_vm_rand_set },
	{ "rand-sum",	stress_vm_rand_sum },
	{ "read64",	stress_vm_read64 },
	{ "ror",	stress_vm_ror },
	{ "swap",	stress_vm_swap },
	{ "move-inv",	stress_vm_moving_inversion },
	{ "modulo-x",	stress_vm_modulo_x },
	{ "prime-0",	stress_vm_prime_zero },
	{ "prime-1",	stress_vm_prime_one },
	{ "prime-gray-0",stress_vm_prime_gray_zero },
	{ "prime-gray-1",stress_vm_prime_gray_one },
	{ "prime-incdec",stress_vm_prime_incdec },
	{ "walk-0d",	stress_vm_walking_zero_data },
	{ "walk-1d",	stress_vm_walking_one_data },
	{ "walk-0a",	stress_vm_walking_zero_addr },
	{ "walk-1a",	stress_vm_walking_one_addr },
	{ "write64",	stress_vm_write64 },
	{ "zero-one",	stress_vm_zero_one },
	{ NULL,		NULL  }
};

/*
 *  stress_set_vm_method()
 *      set default vm stress method
 */
int stress_set_vm_method(const char *name)
{
	stress_vm_stressor_info_t const *info;

	for (info = vm_methods; info->func; info++) {
		if (!strcmp(info->name, name)) {
			opt_vm_stressor = info;
			return 0;
		}
	}

	(void)fprintf(stderr, "vm-method must be one of:");
	for (info = vm_methods; info->func; info++) {
		(void)fprintf(stderr, " %s", info->name);
	}
	(void)fprintf(stderr, "\n");

	return -1;
}


/*
 *  stress_vm()
 *	stress virtual memory
 */
int stress_vm(const args_t *args)
{
	uint64_t *bit_error_count = MAP_FAILED;
	uint32_t restarts = 0, nomems = 0;
	uint8_t *buf = NULL;
	pid_t pid;
	const bool keep = (g_opt_flags & OPT_FLAGS_VM_KEEP);
	const stress_vm_func func = opt_vm_stressor->func;
        const size_t page_size = args->page_size;
	size_t buf_sz, retries;
	int err = 0, ret = EXIT_SUCCESS;

	if (!set_vm_bytes) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			opt_vm_bytes = MAX_VM_BYTES;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			opt_vm_bytes = MIN_VM_BYTES;
	}
	buf_sz = opt_vm_bytes & ~(page_size - 1);

	for (retries = 0; (retries < 100) && g_keep_stressing_flag; retries++) {
		bit_error_count = (uint64_t *)
			mmap(NULL, page_size, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		err = errno;
		if (bit_error_count != MAP_FAILED)
			break;
		(void)shim_usleep(100);
	}

	/* Cannot allocate a single page for bit error counter */
	if (bit_error_count == MAP_FAILED) {
		if (g_keep_stressing_flag) {
			pr_err("%s: could not mmap bit error counter: "
				"retry count=%zu, errno=%d (%s)\n",
				args->name, retries, err, strerror(err));
		}
		return EXIT_NO_RESOURCE;
	}

	*bit_error_count = 0ULL;

again:
	if (!g_keep_stressing_flag)
		goto clean_up;
	pid = fork();
	if (pid < 0) {
		if (errno == EAGAIN)
			goto again;
		pr_err("%s: fork failed: errno=%d: (%s)\n",
			args->name, errno, strerror(errno));
	} else if (pid > 0) {
		int status, waitret;

		/* Parent, wait for child */
		(void)setpgid(pid, g_pgrp);
		waitret = waitpid(pid, &status, 0);
		if (waitret < 0) {
			if (errno != EINTR)
				pr_dbg("%s: waitpid(): errno=%d (%s)\n",
					args->name, errno, strerror(errno));
			(void)kill(pid, SIGTERM);
			(void)kill(pid, SIGKILL);
			(void)waitpid(pid, &status, 0);
		} else if (WIFSIGNALED(status)) {
			pr_dbg("%s: child died: %s (instance %d)\n",
				args->name, stress_strsignal(WTERMSIG(status)),
				args->instance);
			/* If we got killed by OOM killer, re-start */
			if (WTERMSIG(status) == SIGKILL) {
				log_system_mem_info();
				pr_dbg("%s: assuming killed by OOM killer, "
					"restarting again (instance %d)\n",
					args->name, args->instance);
				restarts++;
				goto again;
			}
		}
	} else if (pid == 0) {
		int no_mem_retries = 0;

		(void)setpgid(0, g_pgrp);
		stress_parent_died_alarm();

		/* Make sure this is killable by OOM killer */
		set_oom_adjustment(args->name, true);

		do {
			if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
				pr_err("%s: gave up trying to mmap, no available memory\n",
					args->name);
				break;
			}
			if (!keep || (buf == NULL)) {
				if (!g_keep_stressing_flag)
					return EXIT_SUCCESS;
				buf = (uint8_t *)mmap(NULL, buf_sz,
					PROT_READ | PROT_WRITE,
					MAP_SHARED | MAP_ANONYMOUS |
					opt_vm_flags, -1, 0);
				if (buf == MAP_FAILED) {
					buf = NULL;
					no_mem_retries++;
					(void)shim_usleep(100000);
					continue;	/* Try again */
				}
				(void)madvise_random(buf, buf_sz);
			}

			no_mem_retries = 0;
			(void)mincore_touch_pages(buf, buf_sz);
			*bit_error_count += func(buf, buf_sz, args->counter,
						args->max_ops << VM_BOGO_SHIFT);

			if (opt_vm_hang == 0) {
				for (;;) {
					(void)sleep(3600);
				}
			} else if (opt_vm_hang != DEFAULT_VM_HANG) {
				(void)sleep((int)opt_vm_hang);
			}

			if (!keep) {
				(void)madvise_random(buf, buf_sz);
				(void)munmap((void *)buf, buf_sz);
			}
		} while (keep_stressing());

		if (keep && buf != NULL)
			(void)munmap((void *)buf, buf_sz);
	}
clean_up:
	(void)shim_msync(bit_error_count, page_size, MS_SYNC);
	if (*bit_error_count > 0) {
		pr_fail("%s: detected %" PRIu64 " bit errors while "
			"stressing memory\n",
			args->name, *bit_error_count);
		ret = EXIT_FAILURE;
	}
	(void)munmap(bit_error_count, page_size);

	*args->counter >>= VM_BOGO_SHIFT;

	if (restarts + nomems > 0)
		pr_dbg("%s: OOM restarts: %" PRIu32
			", out of memory restarts: %" PRIu32 ".\n",
			args->name, restarts, nomems);

	return ret;
}
