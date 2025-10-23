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
#include "core-asm-generic.h"
#include "core-asm-x86.h"
#include "core-attribute.h"
#include "core-builtin.h"
#include "core-cpu-cache.h"
#include "core-target-clones.h"
#include "core-madvise.h"
#include "core-mincore.h"
#include "core-mmap.h"
#include "core-nt-load.h"
#include "core-nt-store.h"
#include "core-numa.h"
#include "core-out-of-memory.h"
#include "core-pragma.h"
#include "core-prime.h"
#include "core-vecmath.h"

#define MIN_VM_BYTES		(4 * KB)
#define MAX_VM_BYTES		(MAX_MEM_LIMIT)
#define DEFAULT_VM_BYTES	(256 * MB)

#define MIN_VM_HANG		(0)
#define MAX_VM_HANG		(3600)
#define DEFAULT_VM_HANG		(~0ULL)

/* For testing, set this to 1 to simulate random memory errors */
#define INJECT_BIT_ERRORS	(0)

#define VM_BOGO_SHIFT		(12)
#define VM_ROWHAMMER_LOOPS	(1000000)

#define NO_MEM_RETRIES_MAX	(32)

static size_t stress_vm_cache_line_size;
static bool vm_flush;

/*
 *  the VM stress test has different methods of vm stressor
 */
typedef size_t (*stress_vm_func)(void *buf, void *buf_end, const size_t sz,
		stress_args_t *args, const uint64_t max_ops);

typedef struct {
	const char *name;
	const stress_vm_func func;
} stress_vm_method_info_t;

typedef struct {
	const char *name;
	const int advice;
} stress_vm_madvise_info_t;

typedef struct {
	uint64_t *bit_error_count;
	const stress_vm_method_info_t *vm_method;
#if defined(HAVE_LINUX_MEMPOLICY_H)
	stress_numa_mask_t *numa_mask;
	stress_numa_mask_t *numa_nodes;
#endif
	size_t vm_bytes;
	bool vm_numa;
} stress_vm_context_t;

static const stress_help_t help[] = {
	{ "m N", "vm N",	 "start N workers spinning on anonymous mmap" },
	{ NULL,	 "vm-bytes N",	 "allocate N bytes per vm worker (default 256MB)" },
	{ NULL,  "vm-flush",	 "cache flush data after write" },
	{ NULL,	 "vm-hang N",	 "sleep N seconds before freeing memory" },
	{ NULL,	 "vm-keep",	 "redirty memory instead of reallocating" },
#if defined(MAP_LOCKED)
	{ NULL,	 "vm-locked",	 "lock the pages of the mapped region into memory" },
#endif
	{ NULL,	 "vm-madvise M", "specify mmap'd vm buffer madvise advice" },
	{ NULL,	 "vm-method M",	 "specify stress vm method M, default is all" },
	{ NULL,	 "vm-numa",	 "bind memory mappings to randomly selected NUMA nodes" },
	{ NULL,	 "vm-ops N",	 "stop after N vm bogo operations" },
#if defined(MAP_POPULATE)
	{ NULL,	 "vm-populate",	 "populate (prefault) page tables for a mapping" },
#endif
	{ NULL,	 NULL,		 NULL }
};

static const stress_vm_madvise_info_t vm_madvise_info[] = {
#if !defined(HAVE_MADVISE)
	/* No MADVISE, default to normal, ignored */
	{ "normal",	0 },
#else
#if defined(MADV_COLLAPSE)
	{ "collapse",	MADV_COLLAPSE },
#endif
#if defined(MADV_DONTNEED)
	{ "dontneed",	MADV_DONTNEED },
#endif
#if defined(MADV_HUGEPAGE)
	{ "hugepage",	MADV_HUGEPAGE },
#endif
#if defined(MADV_MERGEABLE)
	{ "mergeable",	MADV_MERGEABLE },
#endif
#if defined(MADV_NOHUGEPAGE)
	{ "nohugepage",	MADV_NOHUGEPAGE },
#endif
#if defined(MADV_NORMAL)
	{ "normal",	MADV_NORMAL },
#endif
#if defined(MADV_RANDOM)
	{ "random",	MADV_RANDOM },
#endif
#if defined(MADV_SEQUENTIAL)
	{ "sequential",	MADV_SEQUENTIAL },
#endif
#if defined(MADV_UNMERGEABLE)
	{ "unmergeable",MADV_UNMERGEABLE },
#endif
#if defined(MADV_WILLNEED)
	{ "willneed",	MADV_WILLNEED},
#endif
#endif
};

/*
 *  stress_continue(args)
 *	returns true if we can keep on running a stressor
 */
static bool OPTIMIZE3 stress_continue_vm(stress_args_t *args)
{
	return (LIKELY(stress_continue_flag()) &&
	        LIKELY(!args->bogo.max_ops || ((stress_bogo_get(args) >> VM_BOGO_SHIFT) < args->bogo.max_ops)));
}

#define SET_AND_TEST(ptr, val, bit_errors)	\
do {						\
	*ptr = val;				\
	bit_errors += (*ptr != val);		\
} while (0)

#define INC_LO_NYBBLE(val)			\
do {						\
	uint8_t lo = (val);			\
	lo += 1;				\
	lo &= 0xf;				\
	(val) = ((val) & 0xf0) | lo;		\
} while (0)

#define INC_HI_NYBBLE(val)			\
do {						\
	uint8_t hi = (val);			\
	hi += 0xf0;				\
	hi &= 0xf0;				\
	(val) = ((val) & 0x0f) | hi;		\
} while (0)

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
		buf[stress_mwc64modn(sz)] ^= (1 << i);
		buf[stress_mwc64modn(sz)] |= (1 << i);
		buf[stress_mwc64modn(sz)] &= ~(1 << i);
	}

	for (i = 0; i < 7; i++) {
		/* 2 bit errors */
		buf[stress_mwc64modn(sz)] ^= (3 << i);
		buf[stress_mwc64modn(sz)] |= (3 << i);
		buf[stress_mwc64modn(sz)] &= ~(3 << i);
	}

	for (i = 0; i < 6; i++) {
		/* 3 bit errors */
		buf[stress_mwc64modn(sz)] ^= (7 << i);
		buf[stress_mwc64modn(sz)] |= (7 << i);
		buf[stress_mwc64modn(sz)] &= ~(7 << i);
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
 *  compute a % b where b is normally just larger than b, so we
 *  need to do a - b once and occasionally just twice. Use repeated
 *  subtraction since this is faster than %
 */
static inline ALWAYS_INLINE CONST OPTIMIZE3 uint64_t stress_vm_mod(register uint64_t a, register const size_t b)
{
	if (LIKELY(a >= b))
		a -= b;
	if (UNLIKELY(a >= b))
		a -= b;
	return a;
}

/*
 *  stress_vm_check()
 *	report back on bit errors found
 */
static void stress_vm_check(const char *name, const size_t bit_errors)
{
	if (UNLIKELY(bit_errors && (g_opt_flags & OPT_FLAGS_VERIFY)))
#if INJECT_BIT_ERRORS
		pr_dbg("%s: detected %zu memory error%s\n",
			name, bit_errors, bit_errors == 1 ? "" : "s");
#else
		pr_fail("%s: detected %zu memory error%s\n",
			name, bit_errors, bit_errors == 1 ? "" : "s");
#endif
}

/*
 *  stress_vm_count_bits8()
 *	count number of bits set (K and R)
 */
static inline CONST size_t stress_vm_count_bits8(uint8_t v)
{
#if defined(HAVE_BUILTIN_POPCOUNT)
	return (size_t)__builtin_popcount((unsigned int)v);
#else
	size_t n;

	for (n = 0; v; n++)
		v &= v - 1;
	return n;
#endif
}

/*
 *  stress_vm_count_bits()
 *	count number of bits set (K and R)
 */
static inline size_t CONST stress_vm_count_bits(uint64_t v)
{
#if defined(HAVE_BUILTIN_POPCOUNTLL)
	if (sizeof(unsigned long long int) == sizeof(uint64_t)) {
		return (size_t)__builtin_popcountll((unsigned long long int)v);
	}
#endif
#if defined(HAVE_BUILTIN_POPCOUNTL)
	if (sizeof(unsigned long int) == sizeof(uint64_t)) {
		return (size_t)__builtin_popcountl((unsigned long int)v);
	} else if (sizeof(unsigned long int) == sizeof(uint32_t)) {
		const unsigned long int lo = (unsigned long int)(v >> 32);
		const unsigned long int hi = (unsigned long int)(v & 0xffffffff);

		return (size_t)__builtin_popcountl(hi) + __builtin_popcountl(lo);
	}
#endif
#if defined(HAVE_BUILTIN_POPCOUNT)
	if (sizeof(unsigned int) == sizeof(uint32_t)) {
		const unsigned int lo = (unsigned int)(v >> 32);
		const unsigned int hi = (unsigned int)(v & 0xffffffff);

		return (size_t)__builtin_popcount(hi) + __builtin_popcount(lo);
	}
#else
	size_t n;

	for (n = 0; v; n++)
		v &= v - 1;
	return n;
#endif
}

/*
 *  stress_vm_moving_inversion()
 *	work sequentially through memory setting 8 bytes at a time
 *	with a random value, then check if it is correct, invert it and
 *	then check if that is correct.
 */
static size_t TARGET_CLONES stress_vm_moving_inversion(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	uint64_t c = stress_bogo_get(args);
	uint32_t w, z;
	register uint64_t *ptr;
	size_t bit_errors;

	stress_mwc_reseed();
	w = stress_mwc32();
	z = stress_mwc32();

	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ) {
		*(ptr++) = stress_mwc64();
	}

	stress_mwc_set_seed(w, z);
	for (bit_errors = 0, ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ) {
		const uint64_t val = stress_mwc64();

		if (UNLIKELY(*ptr != val))
			bit_errors++;
		*(ptr++) = ~val;
		stress_asm_mb();
	}
	c += sz / sizeof(*ptr);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto ret;
	if (UNLIKELY(!stress_continue_flag()))
		goto ret;

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);

	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w, z);
	for (bit_errors = 0, ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ) {
		const uint64_t val = stress_mwc64();

		if (UNLIKELY(*(ptr++) != ~val))
			bit_errors++;
	}
	c += sz / sizeof(*ptr);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto ret;
	if (UNLIKELY(!stress_continue_flag()))
		goto ret;

	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		*--ptr = stress_mwc64();
	}
	if (UNLIKELY(!stress_continue_flag()))
		goto ret;

	inject_random_bit_errors(buf, sz);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		register const uint64_t val = stress_mwc64();

		if (UNLIKELY(*--ptr != val))
			bit_errors++;
		*ptr = ~val;
		stress_asm_mb();
	}
	c += sz / sizeof(*ptr);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto ret;
	if (UNLIKELY(!stress_continue_flag()))
		goto ret;

	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf_end; ptr > (uint64_t *)buf; ) {
		register const uint64_t val = stress_mwc64();

		if (UNLIKELY(*--ptr != ~val))
			bit_errors++;
	}
	c += sz / sizeof(*ptr);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto ret;
	if (UNLIKELY(!stress_continue_flag()))
		goto ret;

ret:
	stress_vm_check("moving inversion", bit_errors);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		c = max_ops;
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_modulo_x()
 *	set every 23rd byte to a random pattern and then set
 *	all the other bytes to the complement of this. Check
 *	that the random patterns are still set.
 */
static size_t TARGET_CLONES stress_vm_modulo_x(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	uint32_t i, j;
	const uint32_t stride = 23;	/* Small prime to hit cache */
	register uint8_t pattern, compliment;
	register uint8_t *ptr;
	size_t bit_errors = 0;
	uint64_t c = stress_bogo_get(args);

	stress_mwc_reseed();
	pattern = stress_mwc8();
	compliment = ~pattern;

	for (i = 0; i < stride; i++) {
		for (ptr = (uint8_t *)buf + i; ptr < (uint8_t *)buf_end; ptr += stride) {
			*ptr = pattern;
		}
		if (UNLIKELY(!stress_continue_flag()))
			goto ret;
		for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += stride) {
			for (j = 0; (j < i) && (ptr < (uint8_t *)buf_end); j++, c++) {
				*ptr++ = compliment;
				stress_asm_mb();
			}
			if (UNLIKELY(!stress_continue_flag()))
				goto ret;
			ptr++;
			for (j = i + 1; (j < stride) && (ptr < (uint8_t *)buf_end); j++, c++) {
				*ptr++ = compliment;
				stress_asm_mb();
			}
			if (UNLIKELY(!stress_continue_flag()))
				goto ret;
		}
		inject_random_bit_errors(buf, sz);

		for (ptr = (uint8_t *)buf + i; ptr < (uint8_t *)buf_end; ptr += stride) {
			if (UNLIKELY(*ptr != pattern))
				bit_errors++;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}

	if (UNLIKELY(max_ops && (c >= max_ops)))
		c = max_ops;
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("modulo X", bit_errors);
ret:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_walking_one_data()
 *	for each byte, walk through each data line setting them to high
 *	setting each bit to see if none of the lines are stuck
 */
static size_t TARGET_CLONES stress_vm_walking_one_data(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint8_t *ptr;
	register uint64_t c = stress_bogo_get(args);

	(void)sz;

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		SET_AND_TEST(ptr, 0x01, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x02, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x04, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x08, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x10, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x20, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x40, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x80, bit_errors);
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("walking one (data)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_walking_zero_data()
 *	for each byte, walk through each data line setting them to low
 *	setting each bit to see if none of the lines are stuck
 */
static size_t TARGET_CLONES stress_vm_walking_zero_data(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint8_t *ptr;
	register uint64_t c = stress_bogo_get(args);

	(void)sz;

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		SET_AND_TEST(ptr, 0xfe, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xfd, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xfb, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xf7, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xef, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xdf, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0xbf, bit_errors);
		stress_asm_mb();
		SET_AND_TEST(ptr, 0x7f, bit_errors);
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("walking zero (data)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_walking_one_addr()
 *	work through a range of addresses setting each address bit in
 *	the given memory mapped range to high to see if any address bits
 *	are stuck.
 */
static size_t TARGET_CLONES stress_vm_walking_one_addr(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	uint8_t d1 = 0, d2 = ~d1;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	(void)shim_memset(buf, d1, sz);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 256) {
		uint16_t i;
		uint64_t mask;

		*ptr = d1;
		for (mask = 1, i = 1; i < 64; i++) {
			const uintptr_t uintptr = ((uintptr_t)ptr) ^ mask;
			uint8_t *addr = (uint8_t *)uintptr;
			if ((addr < (uint8_t *)buf) || (addr >= (uint8_t *)buf_end) || (addr == ptr))
				continue;
			*addr = d2;
			stress_asm_mb();
			if (UNLIKELY(*ptr != d1)) /* cppcheck-suppress knownConditionTrueFalse */
				bit_errors++;
			mask <<= 1;
		}
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("walking one (address)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_walking_zero_addr()
 *	work through a range of addresses setting each address bit in
 *	the given memory mapped range to low to see if any address bits
 *	are stuck.
 */
static size_t TARGET_CLONES stress_vm_walking_zero_addr(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	uint8_t d1 = 0, d2 = ~d1;
	size_t bit_errors = 0;
	uint64_t sz_mask;
	register uint64_t c = stress_bogo_get(args);

	for (sz_mask = 1; sz_mask < sz; sz_mask <<= 1)
		;

	sz_mask--;

	(void)shim_memset(buf, d1, sz);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 256) {
		uint16_t i;
		uint64_t mask;

		*ptr = d1;
		for (mask = 1, i = 1; i < 64; i++) {
			const uintptr_t uintptr = ((uintptr_t)ptr) ^ (~mask & sz_mask);
			uint8_t *addr = (uint8_t *)uintptr;
			if ((addr < (uint8_t *)buf) || (addr >= (uint8_t *)buf_end) || (addr == ptr))
				continue;
			*addr = d2;
			stress_asm_mb();
			if (UNLIKELY(*ptr != d1)) /* cppcheck-suppress knownConditionTrueFalse */
				bit_errors++;
			mask <<= 1;
		}
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("walking zero (address)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_walking_flush_data()
 *	for each byte, walk through each byte flushing data
 */
static size_t TARGET_CLONES stress_vm_walking_flush_data(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint8_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	register uint8_t val = 0;

	(void)sz;

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end - 7; ptr++, val++) {
		*(ptr + 0) = (val + 0) & 0xff;
		shim_clflush(ptr + 0);
		stress_asm_mb();
		*(ptr + 1) = (val + 1) & 0xff;
		shim_clflush(ptr + 1);
		stress_asm_mb();
		*(ptr + 2) = (val + 2) & 0xff;
		shim_clflush(ptr + 2);
		stress_asm_mb();
		*(ptr + 3) = (val + 3) & 0xff;
		shim_clflush(ptr + 3);
		stress_asm_mb();
		*(ptr + 4) = (val + 4) & 0xff;
		shim_clflush(ptr + 4);
		stress_asm_mb();
		*(ptr + 5) = (val + 5) & 0xff;
		shim_clflush(ptr + 5);
		stress_asm_mb();
		*(ptr + 6) = (val + 6) & 0xff;
		shim_clflush(ptr + 6);
		stress_asm_mb();
		*(ptr + 7) = (val + 7) & 0xff;
		shim_mfence();

		bit_errors += (*(ptr + 0) != ((val + 0) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 1) != ((val + 1) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 2) != ((val + 2) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 3) != ((val + 3) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 4) != ((val + 4) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 5) != ((val + 5) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 6) != ((val + 6) & 0xff));
		stress_asm_mb();
		bit_errors += (*(ptr + 7) != ((val + 7) & 0xff));
		stress_asm_mb();

		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("walking flush (data)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}


/*
 *  stress_vm_gray()
 *	fill all of memory with a gray code and check that
 *	all the bits are set correctly. gray codes just change
 *	one bit at a time.
 */
static size_t TARGET_CLONES OPTIMIZE3 stress_vm_gray(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	register uint8_t v;
	register uint8_t *ptr;
	register size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	for (v = val, ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ) {
		register uint8_t mask;

		mask = (v >> 1) ^ v;
		v++;
		*(ptr++) = mask;
		stress_asm_mb();

		mask = (v >> 1) ^ v;
		v++;
		*(ptr++) = mask;
		stress_asm_mb();

		mask = (v >> 1) ^ v;
		v++;
		*(ptr++) = mask;
		stress_asm_mb();

		mask = (v >> 1) ^ v;
		v++;
		*(ptr++) = mask;

		if (UNLIKELY(!stress_continue_flag()))
			return 0;
	}
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (v = val, ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ) {
		register uint8_t mask;

		mask = (v >> 1) ^ v;
		v++;
		if (UNLIKELY(*(ptr++) != mask))
			bit_errors++;

		mask = (v >> 1) ^ v;
		v++;
		if (UNLIKELY(*(ptr++) != mask))
			bit_errors++;

		mask = (v >> 1) ^ v;
		v++;
		if (UNLIKELY(*(ptr++) != mask))
			bit_errors++;

		mask = (v >> 1) ^ v;
		v++;
		if (UNLIKELY(*(ptr++) != mask))
			bit_errors++;

		c += 4;
		if (UNLIKELY(!stress_continue_flag()))
			break;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}
	val++;

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("gray code", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_grayflip()
 *	fill all of memory with a gray code based pattern that
 *	flips as many bits as possible on each write.
 */
static size_t TARGET_CLONES stress_vm_grayflip(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	uint8_t v;
	register uint8_t *ptr;
	size_t bit_errors = 0;
	const uint64_t c_orig = stress_bogo_get(args);
	register uint64_t c;

	for (c = c_orig, v = val, ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; v++) {
		register uint8_t gray;

		if (UNLIKELY(!stress_continue_flag()))
			return 0;

		gray = (v >> 1) ^ v;
		*ptr++ = gray;
		stress_asm_mb();

		gray = ~gray;
		*ptr++ = gray;
		stress_asm_mb();

		gray = (v >> 1) ^ v;
		*ptr++ = gray;
		stress_asm_mb();

		gray = ~gray;
		*ptr++ = gray;
	}
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (v = val, ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; v++) {
		register uint8_t gray;

		if (UNLIKELY(!stress_continue_flag()))
			break;

		gray = (v >> 1) ^ v;
		if (UNLIKELY(*(ptr++) != gray))
			bit_errors++;
		gray = ~gray;
		if (UNLIKELY(*(ptr++) != gray))
			bit_errors++;
		gray = (v >> 1) ^ v;
		if (UNLIKELY(*(ptr++) != gray))
			bit_errors++;
		gray = ~gray;
		if (UNLIKELY(*(ptr++) != gray))
			bit_errors++;
		c += 4;

		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}
	val++;

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("gray code (flip)", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}


/*
 *  stress_vm_incdec()
 *	work through memory incrementing it and then decrementing
 *	it by a value that changes on each test iteration.
 *	Check that the memory has not changed by the inc + dec
 *	operations.
 */
static size_t TARGET_CLONES stress_vm_incdec(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	register uint8_t *ptr;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	val++;
	(void)shim_memset(buf, 0x00, sz);

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		*ptr += val;
		stress_asm_mb();
	}
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		*ptr -= val;
		stress_asm_mb();
	}
	c += sz;
	if (UNLIKELY(max_ops && (c >= max_ops)))
		c = max_ops;

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		if (UNLIKELY(*ptr != 0))
			bit_errors++;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("incdec code", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_prime_incdec()
 *	walk through memory in large prime steps incrementing
 *	bytes and then re-walk again decrementing; then sanity
 *	check.
 */
static size_t TARGET_CLONES stress_vm_prime_incdec(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	register uint8_t *ptr = buf;
	size_t bit_errors = 0, i;
	const uint64_t prime = stress_get_prime64(sz + 4096);
	register uint64_t j, c;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (UNLIKELY(sz > (1ULL << 63)))
		return 0;
#endif

	(void)shim_memset(buf, 0x00, sz);

	c = stress_bogo_get(args);
	for (i = 0; i < sz; i++) {
		ptr[i] += val;
		stress_asm_mb();
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	c = stress_bogo_get(args);
	for (i = 0, j = prime; i < sz; i++, j += prime) {
		j = stress_vm_mod(j, sz);
		ptr[j] -= val;
		stress_asm_mb();
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++) {
		if (UNLIKELY(*ptr != 0))
			bit_errors++;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("prime-incdec", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_swap()
 *	forward swap and then reverse swap chunks of memory
 *	and see that nothing got corrupted.
 */
static size_t TARGET_CLONES stress_vm_swap(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	const size_t chunk_sz = 64, chunks = sz / chunk_sz;
	register uint64_t c = stress_bogo_get(args);
	uint32_t w1, z1;
	register uint8_t *ptr;
	size_t bit_errors = 0, i;
	size_t *swaps;

	stress_mwc_reseed();
	z1 = stress_mwc32();
	w1 = stress_mwc32();

	if ((swaps = (size_t *)calloc(chunks, sizeof(*swaps))) == NULL) {
		pr_fail("%s: calloc failed on vm_swap\n", args->name);
		return 0;
	}

	for (i = 0; i < chunks; i++) {
		swaps[i] = (stress_mwc64modn(chunks)) * chunk_sz;
	}

	stress_mwc_set_seed(w1, z1);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		const uint8_t val = stress_mwc8();

		(void)shim_memset((void *)ptr, val, chunk_sz);
	}

	/* Forward swaps */
	for (i = 0, ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz, i++) {
		size_t offset = swaps[i];

		uint8_t *dst = (uint8_t *)buf + offset;
		uint8_t *src = (uint8_t *)ptr;
		const uint8_t *src_end = src + chunk_sz;

		while (src < src_end) {
			const uint8_t tmp = *src;

			*src++ = *dst;
			stress_asm_mb();
			*dst++ = tmp;
		}
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	/* Reverse swaps */
	for (i = chunks - 1, ptr = (uint8_t *)buf_end - chunk_sz; ptr >= (uint8_t *)buf; ptr -= chunk_sz, i--) {
		size_t offset = swaps[i];

		uint8_t *dst = (uint8_t *)buf + offset;
		uint8_t *src = (uint8_t *)ptr;
		const uint8_t *src_end = src + chunk_sz;

		while (src < src_end) {
			const uint8_t tmp = *src;

			*src++ = *dst;
			stress_asm_mb();
			*dst++ = tmp;
		}
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w1, z1);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		const uint8_t *p = (uint8_t *)ptr;
		const uint8_t *p_end = (uint8_t *)ptr + chunk_sz;
		uint8_t val = stress_mwc8();

		while (p < p_end) {
			if (UNLIKELY(*p != val))
				bit_errors++;
			p++;
		}
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("swap bytes", bit_errors);
abort:
	free(swaps);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_rand_set()
 *	fill 64 bit chunks of memory with a random pattern and
 *	and then sanity check they are all set correctly.
 */
static size_t TARGET_CLONES stress_vm_rand_set(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	const size_t chunk_sz = sizeof(*ptr) * 8;
	register uint64_t c = stress_bogo_get(args);
	uint32_t w, z;
	size_t bit_errors = 0;

	stress_mwc_reseed();
	w = stress_mwc32();
	z = stress_mwc32();

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		const uint8_t val = stress_mwc8();

		*(ptr + 0) = val;
		stress_asm_mb();
		*(ptr + 1) = val;
		stress_asm_mb();
		*(ptr + 2) = val;
		stress_asm_mb();
		*(ptr + 3) = val;
		stress_asm_mb();
		*(ptr + 4) = val;
		stress_asm_mb();
		*(ptr + 5) = val;
		stress_asm_mb();
		*(ptr + 6) = val;
		stress_asm_mb();
		*(ptr + 7) = val;
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		const uint8_t val = stress_mwc8();

		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("rand-set", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_ror()
 *	fill memory with a random pattern and then rotate
 *	right all the bits in an 8 byte (64 bit) chunk
 *	and then sanity check they are all shifted at the
 *	end.
 */
static size_t TARGET_CLONES stress_vm_ror(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	uint32_t w, z;
	size_t bit_errors = 0;
	const size_t chunk_sz = sizeof(*ptr) * 8;

	stress_mwc_reseed();
	w = stress_mwc32();
	z = stress_mwc32();

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		const uint8_t val = stress_mwc8();

		*(ptr + 0) = val;
		stress_asm_mb();
		*(ptr + 1) = val;
		stress_asm_mb();
		*(ptr + 2) = val;
		stress_asm_mb();
		*(ptr + 3) = val;
		stress_asm_mb();
		*(ptr + 4) = val;
		stress_asm_mb();
		*(ptr + 5) = val;
		stress_asm_mb();
		*(ptr + 6) = val;
		stress_asm_mb();
		*(ptr + 7) = val;
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		*(ptr + 0) = shim_ror8(*(ptr + 0));
		stress_asm_mb();
		*(ptr + 1) = shim_ror8(*(ptr + 1));
		stress_asm_mb();
		*(ptr + 2) = shim_ror8(*(ptr + 2));
		stress_asm_mb();
		*(ptr + 3) = shim_ror8(*(ptr + 3));
		stress_asm_mb();
		*(ptr + 4) = shim_ror8(*(ptr + 4));
		stress_asm_mb();
		*(ptr + 5) = shim_ror8(*(ptr + 5));
		stress_asm_mb();
		*(ptr + 6) = shim_ror8(*(ptr + 6));
		stress_asm_mb();
		*(ptr + 7) = shim_ror8(*(ptr + 7));
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);

	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		uint8_t val = stress_mwc8();
		val = shim_ror8(val);

		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("ror", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_flip()
 *	set all memory to random pattern, then work through
 *	memory 8 times flipping bits 0..7 on by one to eventually
 *	invert all the bits.  Check if the final bits are all
 *	correctly inverted.
 */
static size_t TARGET_CLONES stress_vm_flip(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	register uint8_t bit = 0x03;
	register uint64_t c = stress_bogo_get(args);
	uint32_t w, z;
	size_t bit_errors = 0, i;
	const size_t chunk_sz = sizeof(*ptr) * 8;

	stress_mwc_reseed();
	w = stress_mwc32();
	z = stress_mwc32();

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		register uint8_t val = stress_mwc8();

		*(ptr + 0) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 1) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 2) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 3) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 4) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 5) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 6) = val;
		stress_asm_mb();
		val = shim_ror8(val);
		*(ptr + 7) = val;
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);

	for (i = 0; i < 8; i++) {
		bit = shim_ror8(bit);

		for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
			*(ptr + 0) ^= bit;
			stress_asm_mb();
			*(ptr + 1) ^= bit;
			stress_asm_mb();
			*(ptr + 2) ^= bit;
			stress_asm_mb();
			*(ptr + 3) ^= bit;
			stress_asm_mb();
			*(ptr + 4) ^= bit;
			stress_asm_mb();
			*(ptr + 5) ^= bit;
			stress_asm_mb();
			*(ptr + 6) ^= bit;
			stress_asm_mb();
			*(ptr + 7) ^= bit;
			c++;
			if (UNLIKELY(max_ops && (c >= max_ops)))
				goto abort;
			if (UNLIKELY(!stress_continue_flag()))
				goto abort;
		}
		(void)stress_mincore_touch_pages(buf, sz);
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w, z);
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += chunk_sz) {
		register uint8_t val = stress_mwc8();

		bit_errors += (*(ptr + 0) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 1) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 2) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 3) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 4) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 5) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 6) != val);
		val = shim_ror8(val);
		bit_errors += (*(ptr + 7) != val);
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("flip", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_one_zone()
 *	set all memory to one and see if any bits are stuck at zero and
 *	set all memory to zero and see if any bits are stuck at one
 */
static size_t TARGET_CLONES stress_vm_one_zero(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	size_t bit_errors = 0;

	(void)max_ops;

	(void)shim_memset(buf, 0xff, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(~*(ptr + 0));
		bit_errors += stress_vm_count_bits(~*(ptr + 1));
		bit_errors += stress_vm_count_bits(~*(ptr + 2));
		bit_errors += stress_vm_count_bits(~*(ptr + 3));
		bit_errors += stress_vm_count_bits(~*(ptr + 4));
		bit_errors += stress_vm_count_bits(~*(ptr + 5));
		bit_errors += stress_vm_count_bits(~*(ptr + 6));
		bit_errors += stress_vm_count_bits(~*(ptr + 7));

		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	(void)shim_memset(buf, 0x00, sz);
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(*(ptr + 0));
		bit_errors += stress_vm_count_bits(*(ptr + 1));
		bit_errors += stress_vm_count_bits(*(ptr + 2));
		bit_errors += stress_vm_count_bits(*(ptr + 3));
		bit_errors += stress_vm_count_bits(*(ptr + 4));
		bit_errors += stress_vm_count_bits(*(ptr + 5));
		bit_errors += stress_vm_count_bits(*(ptr + 6));
		bit_errors += stress_vm_count_bits(*(ptr + 7));

		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("one-zero", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_zero_one()
 *	set all memory to zero and see if any bits are stuck at one and
 *	set all memory to one and see if any bits are stuck at zero
 */
static size_t TARGET_CLONES stress_vm_zero_one(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	size_t bit_errors = 0;

	(void)max_ops;

	(void)shim_memset(buf, 0x00, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(*(ptr + 0));
		bit_errors += stress_vm_count_bits(*(ptr + 1));
		bit_errors += stress_vm_count_bits(*(ptr + 2));
		bit_errors += stress_vm_count_bits(*(ptr + 3));
		bit_errors += stress_vm_count_bits(*(ptr + 4));
		bit_errors += stress_vm_count_bits(*(ptr + 5));
		bit_errors += stress_vm_count_bits(*(ptr + 6));
		bit_errors += stress_vm_count_bits(*(ptr + 7));

		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	(void)shim_memset(buf, 0xff, sz);
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	c += sz / 8;

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bit_errors += stress_vm_count_bits(~*(ptr + 0));
		bit_errors += stress_vm_count_bits(~*(ptr + 1));
		bit_errors += stress_vm_count_bits(~*(ptr + 2));
		bit_errors += stress_vm_count_bits(~*(ptr + 3));
		bit_errors += stress_vm_count_bits(~*(ptr + 4));
		bit_errors += stress_vm_count_bits(~*(ptr + 5));
		bit_errors += stress_vm_count_bits(~*(ptr + 6));
		bit_errors += stress_vm_count_bits(~*(ptr + 7));

		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("zero-one", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_galpat_one()
 *	galloping pattern. Set all bits to zero and flip a few
 *	random bits to one.  Check if this one is pulled down
 *	or pulls its neighbours up.
 */
static size_t TARGET_CLONES stress_vm_galpat_zero(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	size_t i, bit_errors = 0, bits_set = 0;
	size_t bits_bad = sz / 4096;
	register uint64_t c = stress_bogo_get(args);

	(void)shim_memset(buf, 0x00, sz);

	stress_mwc_reseed();

	for (i = 0; i < bits_bad; i++) {
		for (;;) {
			const size_t offset = stress_mwc64modn(sz);
			const uint8_t bit = stress_mwc32() & 3;
			register uint8_t *ptr8 = (uint8_t *)buf + offset;

			if (!*ptr8) {
				*ptr8 |= (1 << bit);
				stress_asm_mb();
				break;
			}
		}
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bits_set += stress_vm_count_bits(*(ptr + 0));
		bits_set += stress_vm_count_bits(*(ptr + 1));
		bits_set += stress_vm_count_bits(*(ptr + 2));
		bits_set += stress_vm_count_bits(*(ptr + 3));
		bits_set += stress_vm_count_bits(*(ptr + 4));
		bits_set += stress_vm_count_bits(*(ptr + 5));
		bits_set += stress_vm_count_bits(*(ptr + 6));
		bits_set += stress_vm_count_bits(*(ptr + 7));
		c++;
		if (UNLIKELY(!stress_continue_flag()))
			goto ret;
	}

	if (bits_set != bits_bad)
		bit_errors += UNSIGNED_ABS(bits_set, bits_bad);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("galpat-zero", bit_errors);
ret:
	if (UNLIKELY(max_ops && (c >= max_ops)))
		c = max_ops;
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_galpat_one()
 *	galloping pattern. Set all bits to one and flip a few
 *	random bits to zero.  Check if this zero is pulled up
 *	or pulls its neighbours down.
 */
static size_t TARGET_CLONES stress_vm_galpat_one(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	size_t i, bit_errors = 0, bits_set = 0;
	size_t bits_bad = sz / 4096;
	register uint64_t c = stress_bogo_get(args);

	(void)shim_memset(buf, 0xff, sz);

	stress_mwc_reseed();

	for (i = 0; i < bits_bad; i++) {
		for (;;) {
			const size_t offset = stress_mwc64modn(sz);
			const uint8_t bit = stress_mwc32() & 3;
			register uint8_t *ptr8 = (uint8_t *)buf + offset;

			if (*ptr8 == 0xff) {
				*ptr8 &= ~(1 << bit);
				stress_asm_mb();
				break;
			}
		}
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		bits_set += stress_vm_count_bits(~(*(ptr + 0)));
		bits_set += stress_vm_count_bits(~(*(ptr + 1)));
		bits_set += stress_vm_count_bits(~(*(ptr + 2)));
		bits_set += stress_vm_count_bits(~(*(ptr + 3)));
		bits_set += stress_vm_count_bits(~(*(ptr + 4)));
		bits_set += stress_vm_count_bits(~(*(ptr + 5)));
		bits_set += stress_vm_count_bits(~(*(ptr + 6)));
		bits_set += stress_vm_count_bits(~(*(ptr + 7)));
		c++;
		if (UNLIKELY(!stress_continue_flag()))
			goto ret;
	}

	if (bits_set != bits_bad)
		bit_errors += UNSIGNED_ABS(bits_set, bits_bad);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("galpat-one", bit_errors);
ret:
	if (UNLIKELY(max_ops && (c >= max_ops)))
		c = max_ops;
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_inc_nybble()
 *	work through memort and bump increment lower nybbles by
 *	1 and upper nybbles by 0xf and sanity check byte.
 */
static size_t TARGET_CLONES stress_vm_inc_nybble(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint8_t val = 0;
	register uint8_t *ptr;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	(void)shim_memset(buf, val, sz);
	INC_LO_NYBBLE(val);
	INC_HI_NYBBLE(val);

	stress_mwc_reseed();
	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 8) {
		INC_LO_NYBBLE(*(ptr + 0));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 1));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 2));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 3));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 4));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 5));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 6));
		stress_asm_mb();
		INC_LO_NYBBLE(*(ptr + 7));
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 8) {
		INC_HI_NYBBLE(*(ptr + 0));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 1));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 2));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 3));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 4));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 5));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 6));
		stress_asm_mb();
		INC_HI_NYBBLE(*(ptr + 7));
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 8) {
		bit_errors += (*(ptr + 0) != val);
		bit_errors += (*(ptr + 1) != val);
		bit_errors += (*(ptr + 2) != val);
		bit_errors += (*(ptr + 3) != val);
		bit_errors += (*(ptr + 4) != val);
		bit_errors += (*(ptr + 5) != val);
		bit_errors += (*(ptr + 6) != val);
		bit_errors += (*(ptr + 7) != val);
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("inc-nybble", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_rand_sum()
 *	sequentially set all memory to random values and then
 *	check if they are still set correctly.
 */
static size_t TARGET_CLONES stress_vm_rand_sum(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	uint32_t w, z;
	size_t bit_errors = 0;
	const size_t chunk_sz = sizeof(*ptr) * 8;

	(void)buf_end;

	stress_mwc_reseed();
	w = stress_mwc32();
	z = stress_mwc32();

	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += chunk_sz) {
		*(ptr + 0) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 1) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 2) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 3) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 4) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 5) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 6) = stress_mwc64();
		stress_asm_mb();
		*(ptr + 7) = stress_mwc64();
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	stress_mwc_set_seed(w, z);
	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += chunk_sz) {
		bit_errors += stress_vm_count_bits(*(ptr + 0) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 1) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 2) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 3) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 4) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 5) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 6) ^ stress_mwc64());
		bit_errors += stress_vm_count_bits(*(ptr + 7) ^ stress_mwc64());
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("rand-sum", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_prime_zero()
 *	step through memory in non-contiguous large steps
 *	and clearing each bit to one (one bit per complete memory cycle)
 *	and check if they are clear.
 */
static size_t TARGET_CLONES stress_vm_prime_zero(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);
	register uint8_t i = 0;
	register const size_t prime = 61; /* prime less than cache line size */
	static size_t offset = 0;
	register uint8_t *ptr;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (UNLIKELY(sz > (1ULL << 63)))
		return 0;
#endif
	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		*ptr = 0xff;
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	for (i = 0; i < 8; i++) {
		register const uint8_t mask = (uint8_t)~(1 << i);

		for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime) {
			*ptr &= mask;
			stress_asm_mb();
			c++;
			if (UNLIKELY(max_ops && (c >= max_ops)))
				goto abort;
		}
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		bit_errors += stress_vm_count_bits8(*ptr);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("prime-zero", bit_errors);
abort:
	offset++;
	if (UNLIKELY(offset >= prime))
		offset = 0;
	stress_bogo_set(args, c);
	return bit_errors;
}

/*
 *  stress_vm_prime_one()
 *	step through memory in non-contiguous large steps
 *	and set each bit to one (one bit per complete memory cycle)
 *	and check if they are set.
 */
static size_t TARGET_CLONES stress_vm_prime_one(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	uint64_t c = stress_bogo_get(args);
	register uint8_t i = 0;
	register const size_t prime = 61; /* prime less than cache line size */
	static size_t offset = 0;
	register uint8_t *ptr;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (UNLIKELY(sz > (1ULL << 63)))
		return 0;
#endif
	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		*ptr = 0x00;
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	for (i = 0; i < 8; i++) {
		register const uint8_t mask = (uint8_t)(1 << i);

		for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime) {
			*ptr |= mask;
			stress_asm_mb();
			c++;
			if (UNLIKELY(max_ops && (c >= max_ops)))
				goto abort;
		}
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		bit_errors += 8 - stress_vm_count_bits8(*ptr);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("prime-one", bit_errors);
abort:
	offset++;
	if (UNLIKELY(offset >= prime))
		offset = 0;
	stress_bogo_set(args, c);
	return bit_errors;
}

/*
 *  stress_vm_prime_gray_zero()
 *	step through memory in non-contiguous large steps
 *	and first clear just one bit (based on gray code) and then
 *	clear all the other bits and finally check if they are all clear
 */
static size_t TARGET_CLONES stress_vm_prime_gray_zero(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);
	register uint8_t i = 0;
	register const size_t prime = 61; /* prime less than cache line size */
	static size_t offset = 0;
	register uint8_t *ptr;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (UNLIKELY(sz > (1ULL << 63)))
		return 0;
#endif
	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		*ptr = 0xff;
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	for (i = 0; i < 8; i++) {
		register const uint8_t mask = (uint8_t)((i >> 1) ^ i);

		for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime) {
			*ptr &= mask;
			stress_asm_mb();
			c++;
			if (UNLIKELY(max_ops && (c >= max_ops)))
				goto abort;
		}
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		bit_errors += stress_vm_count_bits8(*ptr);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("prime-gray-zero", bit_errors);
abort:
	offset++;
	if (UNLIKELY(offset >= prime))
		offset = 0;
	stress_bogo_set(args, c);
	return bit_errors;
}

/*
 *  stress_vm_prime_one()
 *	step through memory in non-contiguous large steps
 *	and first set just one bit (based on gray code) and then
 *	set all the other bits and finally check if they are all set
 */
static size_t TARGET_CLONES stress_vm_prime_gray_one(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);
	register uint8_t i = 0;
	register const size_t prime = 61; /* prime less than cache line size */
	static size_t offset = 0;
	register uint8_t *ptr;

#if SIZE_MAX > UINT32_MAX
	/* Unlikely.. */
	if (UNLIKELY(sz > (1ULL << 63)))
		return 0;
#endif
	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		*ptr = 0x00;
	/*
	 *  Step through memory in prime sized steps
	 *  in a totally sub-optimal way to exercise
	 *  memory and cache stalls
	 */
	for (i = 0; i < 8; i++) {
		register const uint8_t mask = (uint8_t)~((i >> 1) ^ i);

		for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime) {
			*ptr |= mask;
			stress_asm_mb();
			c++;
			if (UNLIKELY(max_ops && (c >= max_ops)))
				goto abort;
		}
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += prime)
		bit_errors += 8 - stress_vm_count_bits8(*ptr);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("prime-gray-one", bit_errors);
abort:
	offset++;
	if (UNLIKELY(offset >= prime))
		offset = 0;
	stress_bogo_set(args, c);
	return bit_errors;
}

/*
 *  stress_vm_write_64()
 *	simple 64 bit write, no read check
 */
static size_t OPTIMIZE3 TARGET_CLONES stress_vm_write64(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static uint64_t val;
	register uint64_t *ptr = (uint64_t *)buf;
	register const uint64_t v = val;
	register size_t i = 0;
	register const size_t n = sz / (sizeof(*ptr) * 32);

	(void)buf_end;

	while (i < n) {
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();

		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();

		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();

		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		stress_asm_mb();
		*ptr++ = v;
		i++;
		if (UNLIKELY(!stress_continue_flag() || (max_ops && (i >= max_ops))))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_bogo_add(args, i);
	val++;

	return 0;
}

#if defined(HAVE_ASM_X86_MOVDIRI) &&	\
    defined(STRESS_ARCH_X86_64)
/*
 *  stress_vm_write_64ds()
 *	64 bit direct store write, no read check
 */
static size_t OPTIMIZE3 stress_vm_write64ds(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static bool movdiri = true;

	if (stress_cpu_x86_has_movdiri()) {
		static uint64_t val;
		register uint64_t *ptr = (uint64_t *)buf;
		register const uint64_t v = val;
		register size_t i = 0;
		register const size_t n = sz / (sizeof(*ptr) * 32);

		while (i < n) {
			stress_ds_store64(&ptr[0x00], v);
			stress_ds_store64(&ptr[0x01], v);
			stress_ds_store64(&ptr[0x02], v);
			stress_ds_store64(&ptr[0x03], v);
			stress_ds_store64(&ptr[0x04], v);
			stress_ds_store64(&ptr[0x05], v);
			stress_ds_store64(&ptr[0x06], v);
			stress_ds_store64(&ptr[0x07], v);
			ptr += 8;

			stress_ds_store64(&ptr[0x00], v);
			stress_ds_store64(&ptr[0x01], v);
			stress_ds_store64(&ptr[0x02], v);
			stress_ds_store64(&ptr[0x03], v);
			stress_ds_store64(&ptr[0x04], v);
			stress_ds_store64(&ptr[0x05], v);
			stress_ds_store64(&ptr[0x06], v);
			stress_ds_store64(&ptr[0x07], v);
			ptr += 8;

			stress_ds_store64(&ptr[0x00], v);
			stress_ds_store64(&ptr[0x01], v);
			stress_ds_store64(&ptr[0x02], v);
			stress_ds_store64(&ptr[0x03], v);
			stress_ds_store64(&ptr[0x04], v);
			stress_ds_store64(&ptr[0x05], v);
			stress_ds_store64(&ptr[0x06], v);
			stress_ds_store64(&ptr[0x07], v);
			ptr += 8;

			stress_ds_store64(&ptr[0x00], v);
			stress_ds_store64(&ptr[0x01], v);
			stress_ds_store64(&ptr[0x02], v);
			stress_ds_store64(&ptr[0x03], v);
			stress_ds_store64(&ptr[0x04], v);
			stress_ds_store64(&ptr[0x05], v);
			stress_ds_store64(&ptr[0x06], v);
			stress_ds_store64(&ptr[0x07], v);
			ptr += 8;

			i++;
			if (UNLIKELY(!stress_continue_flag() || (max_ops && (i >= max_ops))))
				break;
		}
		stress_bogo_add(args, i);
		val++;
		if (vm_flush)
			stress_cpu_data_cache_flush(buf, sz);
		return 0;
	}

	if (movdiri && (stress_instance_zero(args))) {
		movdiri = false;

		pr_inf("%s: x86 movdiri instruction not supported, "
			"dropping back to plain 64 bit writes\n", args->name);
	}

	return stress_vm_write64(buf, buf_end, sz, args, max_ops);
}
#endif

#if defined(HAVE_NT_STORE64)
/*
 *  stress_vm_write_64nt()
 *	64 bit non-temporal write, no read check
 */
static size_t TARGET_CLONES stress_vm_write64nt(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static bool nt_store = true;

	if (stress_cpu_x86_has_sse2()) {
		static uint64_t val;
		register uint64_t *ptr = (uint64_t *)buf;
		register const uint64_t v = val;
		register size_t i = 0;
		register const size_t n = sz / (sizeof(*ptr) * 32);

		while (i < n) {
			stress_nt_store64(&ptr[0x00], v);
			stress_nt_store64(&ptr[0x01], v);
			stress_nt_store64(&ptr[0x02], v);
			stress_nt_store64(&ptr[0x03], v);
			stress_nt_store64(&ptr[0x04], v);
			stress_nt_store64(&ptr[0x05], v);
			stress_nt_store64(&ptr[0x06], v);
			stress_nt_store64(&ptr[0x07], v);
			ptr += 8;

			stress_nt_store64(&ptr[0x00], v);
			stress_nt_store64(&ptr[0x01], v);
			stress_nt_store64(&ptr[0x02], v);
			stress_nt_store64(&ptr[0x03], v);
			stress_nt_store64(&ptr[0x04], v);
			stress_nt_store64(&ptr[0x05], v);
			stress_nt_store64(&ptr[0x06], v);
			stress_nt_store64(&ptr[0x07], v);
			ptr += 8;

			stress_nt_store64(&ptr[0x00], v);
			stress_nt_store64(&ptr[0x01], v);
			stress_nt_store64(&ptr[0x02], v);
			stress_nt_store64(&ptr[0x03], v);
			stress_nt_store64(&ptr[0x04], v);
			stress_nt_store64(&ptr[0x05], v);
			stress_nt_store64(&ptr[0x06], v);
			stress_nt_store64(&ptr[0x07], v);
			ptr += 8;

			stress_nt_store64(&ptr[0x00], v);
			stress_nt_store64(&ptr[0x01], v);
			stress_nt_store64(&ptr[0x02], v);
			stress_nt_store64(&ptr[0x03], v);
			stress_nt_store64(&ptr[0x04], v);
			stress_nt_store64(&ptr[0x05], v);
			stress_nt_store64(&ptr[0x06], v);
			stress_nt_store64(&ptr[0x07], v);
			ptr += 8;

			i++;
			if (UNLIKELY(!stress_continue_flag() || (max_ops && (i >= max_ops))))
				break;
		}
		stress_bogo_add(args, i);
		val++;
		if (vm_flush)
			stress_cpu_data_cache_flush(buf, sz);
		return 0;
	}
	if (nt_store && (stress_instance_zero(args))) {
		nt_store = false;

		pr_inf("%s: x86 movnti instruction not supported, "
			"dropping back to plain 64 bit writes\n", args->name);
	}

	return stress_vm_write64(buf, buf_end, sz, args, max_ops);
}
#endif


/*
 *  stress_vm_read_64()
 *	simple 64 bit read
 */
static size_t OPTIMIZE3 TARGET_CLONES stress_vm_read64(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr = (uint64_t *)buf;
	register size_t i = 0;
	register const size_t n = sz / (sizeof(*ptr) * 32);

	(void)buf_end;

	while (i < n) {
		shim_builtin_prefetch((uint8_t *)ptr + 1024, 0, 3);
		(void)*(volatile uint64_t *)&ptr[0];
		(void)*(volatile uint64_t *)&ptr[1];
		(void)*(volatile uint64_t *)&ptr[2];
		(void)*(volatile uint64_t *)&ptr[3];
		(void)*(volatile uint64_t *)&ptr[4];
		(void)*(volatile uint64_t *)&ptr[5];
		(void)*(volatile uint64_t *)&ptr[6];
		(void)*(volatile uint64_t *)&ptr[7];
		ptr += 8;

		(void)*(volatile uint64_t *)&ptr[0];
		(void)*(volatile uint64_t *)&ptr[1];
		(void)*(volatile uint64_t *)&ptr[2];
		(void)*(volatile uint64_t *)&ptr[3];
		(void)*(volatile uint64_t *)&ptr[4];
		(void)*(volatile uint64_t *)&ptr[5];
		(void)*(volatile uint64_t *)&ptr[6];
		(void)*(volatile uint64_t *)&ptr[7];
		ptr += 8;

		(void)*(volatile uint64_t *)&ptr[0];
		(void)*(volatile uint64_t *)&ptr[1];
		(void)*(volatile uint64_t *)&ptr[2];
		(void)*(volatile uint64_t *)&ptr[3];
		(void)*(volatile uint64_t *)&ptr[4];
		(void)*(volatile uint64_t *)&ptr[5];
		(void)*(volatile uint64_t *)&ptr[6];
		(void)*(volatile uint64_t *)&ptr[7];
		ptr += 8;

		(void)*(volatile uint64_t *)&ptr[0];
		(void)*(volatile uint64_t *)&ptr[1];
		(void)*(volatile uint64_t *)&ptr[2];
		(void)*(volatile uint64_t *)&ptr[3];
		(void)*(volatile uint64_t *)&ptr[4];
		(void)*(volatile uint64_t *)&ptr[5];
		(void)*(volatile uint64_t *)&ptr[6];
		(void)*(volatile uint64_t *)&ptr[7];
		ptr += 8;
		i++;
		if (UNLIKELY(!stress_continue_flag() || (max_ops && (i >= max_ops))))
			break;
	}
	stress_bogo_add(args, i);
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);

	return 0;
}

#if defined(HAVE_VECMATH)
typedef int8_t stress_vint8w1024_t      __attribute__ ((vector_size(1024 / 8)));

/*
 *  stress_vm_write_1024v()
 *	vector 1024 bit write, no read check
 */
static size_t TARGET_CLONES stress_vm_write1024v(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	typedef struct {
		uint64_t v[16];
	} uint64x16_t;

	stress_vint8w1024_t *ptr = (stress_vint8w1024_t *)buf;
	stress_vint8w1024_t v;
	static uint64_t val = 0;
	uint64x16_t *vptr = (uint64x16_t *)&v;
	register size_t i = 0;
	register const size_t n = sz / sizeof(*ptr);

	/* 16 x 64 = 1024 bits, unrolled loop */
	vptr->v[0x0] = val;
	vptr->v[0x1] = val;
	vptr->v[0x2] = val;
	vptr->v[0x3] = val;
	vptr->v[0x4] = val;
	vptr->v[0x5] = val;
	vptr->v[0x6] = val;
	vptr->v[0x7] = val;
	vptr->v[0x8] = val;
	vptr->v[0x9] = val;
	vptr->v[0xa] = val;
	vptr->v[0xb] = val;
	vptr->v[0xc] = val;
	vptr->v[0xd] = val;
	vptr->v[0xe] = val;
	vptr->v[0xf] = val;

	(void)buf_end;

	while (i < n) {
		*ptr++ = v;
		i++;
		if (UNLIKELY(!stress_continue_flag() || (max_ops && (i >= max_ops))))
			break;
	}
	stress_bogo_add(args, i);
	val++;
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);

	return 0;
}
#endif

/*
 *  stress_vm_rowhammer()
 *
 */
static size_t TARGET_CLONES stress_vm_rowhammer(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	uint32_t *buf32 = (uint32_t *)buf;
	static uint32_t val = 0xff5a00a5;
	register size_t j;
	register volatile uint32_t *addr0, *addr1;
	register size_t errors = 0;
	register const size_t n = sz / sizeof(*addr0);
	uint64_t mask = ~(uint64_t)(args->page_size - 1);

	(void)buf_end;
	(void)max_ops;

	if (UNLIKELY(!n)) {
		pr_dbg("stress-vm: rowhammer: zero uint32_t integers could "
			"be hammered, aborting\n");
		return 0;
	}

	(void)stress_mincore_touch_pages(buf, sz);

	for (j = 0; j < n; j++)
		buf32[j] = val;

	/* Pick two random addresses */
	addr0 = &buf32[stress_mwc64modn(n) & mask];
	addr1 = &buf32[stress_mwc64modn(n) & mask];

	/* Hammer the rows */
	for (j = VM_ROWHAMMER_LOOPS / 4; j; j--) {
		*addr0;
		stress_asm_mb();
		*addr1;
		stress_asm_mb();
		shim_clflush(addr0);
		stress_asm_mb();
		shim_clflush(addr1);
		stress_asm_mb();
		shim_mfence();
		stress_asm_mb();
		*addr0;
		stress_asm_mb();
		*addr1;
		stress_asm_mb();
		shim_clflush(addr0);
		stress_asm_mb();
		shim_clflush(addr1);
		stress_asm_mb();
		shim_mfence();
		stress_asm_mb();
		*addr0;
		stress_asm_mb();
		*addr1;
		stress_asm_mb();
		shim_clflush(addr0);
		stress_asm_mb();
		shim_clflush(addr1);
		stress_asm_mb();
		shim_mfence();
		stress_asm_mb();
		*addr0;
		stress_asm_mb();
		*addr1;
		stress_asm_mb();
		shim_clflush(addr0);
		stress_asm_mb();
		shim_clflush(addr1);
		stress_asm_mb();
		shim_mfence();
		stress_asm_mb();
	}
	for (j = 0; j < n; j++)
		if (UNLIKELY(buf32[j] != val))
			errors++;
	if (errors) {
		bit_errors += errors;
		pr_dbg("stress-vm: rowhammer: %zu errors on addresses "
			"%p and %p\n", errors, (volatile void *)addr0, (volatile void *)addr1);
	}
	stress_bogo_add(args, VM_ROWHAMMER_LOOPS);
	val = (val >> 31) | (val << 1);

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("rowhammer", bit_errors);

	return bit_errors;
}

/*
 *  stress_vm_mscan()
 *	for each byte, walk through each bit set to 0, check, set to 1, check
 */
static size_t TARGET_CLONES stress_vm_mscan(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint8_t *ptr = (uint8_t *)buf;
	register const uint8_t *end;
	register uint64_t c = stress_bogo_get(args);

	(void)sz;

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++, c++) {
		*ptr |= 0x01;
		stress_asm_mb();
		*ptr |= 0x02;
		stress_asm_mb();
		*ptr |= 0x04;
		stress_asm_mb();
		*ptr |= 0x08;
		stress_asm_mb();
		*ptr |= 0x10;
		stress_asm_mb();
		*ptr |= 0x20;
		stress_asm_mb();
		*ptr |= 0x40;
		stress_asm_mb();
		*ptr |= 0x80;

		if (UNLIKELY(!stress_continue_flag() || (max_ops && (c >= max_ops))))
			break;
	}
	end = (uint8_t *)ptr;

	stress_bogo_add(args, c);

	for (ptr = (uint8_t *)buf; ptr < end; ptr++) {
		bit_errors += 8 - stress_vm_count_bits8(*ptr);
	}

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr++, c++) {
		*ptr &= 0xfe;
		stress_asm_mb();
		*ptr &= 0xfd;
		stress_asm_mb();
		*ptr &= 0xfb;
		stress_asm_mb();
		*ptr &= 0xf7;
		stress_asm_mb();
		*ptr &= 0xef;
		stress_asm_mb();
		*ptr &= 0xdf;
		stress_asm_mb();
		*ptr &= 0xbf;
		stress_asm_mb();
		*ptr &= 0x7f;

		if (UNLIKELY(!stress_continue_flag() || (max_ops && (c >= max_ops))))
			goto abort;
	}

	stress_bogo_add(args, c);

	end = (uint8_t *)buf_end;
	for (ptr = (uint8_t *)buf; ptr < end; ptr++) {
		bit_errors += stress_vm_count_bits8(*ptr);
	}

abort:
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("mscan", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_cache_stripe()
 *	work through memory in cache chunks and write data
 *	in forward/reverse byte wide stripes
 */
static size_t TARGET_CLONES stress_vm_cache_stripe(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 64) {
		ptr[0x00] = 0xa0;
		stress_asm_mb();
		ptr[0x3f] = 0xcf;
		stress_asm_mb();
		ptr[0x01] = 0xa1;
		stress_asm_mb();
		ptr[0x3e] = 0xce;
		stress_asm_mb();
		ptr[0x02] = 0xa2;
		stress_asm_mb();
		ptr[0x3d] = 0xcd;
		stress_asm_mb();
		ptr[0x03] = 0xa3;
		stress_asm_mb();
		ptr[0x3c] = 0xcc;
		stress_asm_mb();
		ptr[0x04] = 0xa4;
		stress_asm_mb();
		ptr[0x3b] = 0xcb;
		stress_asm_mb();
		ptr[0x05] = 0xa5;
		stress_asm_mb();
		ptr[0x3a] = 0xca;
		stress_asm_mb();
		ptr[0x06] = 0xa6;
		stress_asm_mb();
		ptr[0x39] = 0xc9;
		stress_asm_mb();
		ptr[0x07] = 0xa7;
		stress_asm_mb();
		ptr[0x38] = 0xc8;
		stress_asm_mb();
		ptr[0x08] = 0xa8;
		stress_asm_mb();
		ptr[0x37] = 0xc7;
		stress_asm_mb();
		ptr[0x09] = 0xa9;
		stress_asm_mb();
		ptr[0x36] = 0xc6;
		stress_asm_mb();
		ptr[0x0a] = 0xaa;
		stress_asm_mb();
		ptr[0x35] = 0xc5;
		stress_asm_mb();
		ptr[0x0b] = 0xab;
		stress_asm_mb();
		ptr[0x34] = 0xc4;
		stress_asm_mb();
		ptr[0x0c] = 0xac;
		stress_asm_mb();
		ptr[0x33] = 0xc3;
		stress_asm_mb();
		ptr[0x0d] = 0xad;
		stress_asm_mb();
		ptr[0x32] = 0xc2;
		stress_asm_mb();
		ptr[0x0e] = 0xae;
		stress_asm_mb();
		ptr[0x31] = 0xc1;
		stress_asm_mb();
		ptr[0x0f] = 0xaf;
		stress_asm_mb();
		ptr[0x30] = 0xc0;
		stress_asm_mb();
		ptr[0x10] = 0x50;
		stress_asm_mb();
		ptr[0x2f] = 0x3f;
		stress_asm_mb();
		ptr[0x11] = 0x51;
		stress_asm_mb();
		ptr[0x2e] = 0x3e;
		stress_asm_mb();
		ptr[0x12] = 0x52;
		stress_asm_mb();
		ptr[0x2d] = 0x3d;
		stress_asm_mb();
		ptr[0x13] = 0x53;
		stress_asm_mb();
		ptr[0x2c] = 0x3c;
		stress_asm_mb();
		ptr[0x14] = 0x54;
		stress_asm_mb();
		ptr[0x2b] = 0x3b;
		stress_asm_mb();
		ptr[0x15] = 0x55;
		stress_asm_mb();
		ptr[0x2a] = 0x3a;
		stress_asm_mb();
		ptr[0x16] = 0x56;
		stress_asm_mb();
		ptr[0x29] = 0x39;
		stress_asm_mb();
		ptr[0x17] = 0x57;
		stress_asm_mb();
		ptr[0x28] = 0x38;
		stress_asm_mb();
		ptr[0x18] = 0x58;
		stress_asm_mb();
		ptr[0x27] = 0x37;
		stress_asm_mb();
		ptr[0x19] = 0x59;
		stress_asm_mb();
		ptr[0x25] = 0x35;
		stress_asm_mb();
		ptr[0x1a] = 0x5a;
		stress_asm_mb();
		ptr[0x26] = 0x36;
		stress_asm_mb();
		ptr[0x1b] = 0x5b;
		stress_asm_mb();
		ptr[0x24] = 0x34;
		stress_asm_mb();
		ptr[0x1c] = 0x5c;
		stress_asm_mb();
		ptr[0x23] = 0x33;
		stress_asm_mb();
		ptr[0x1d] = 0x5d;
		stress_asm_mb();
		ptr[0x22] = 0x32;
		stress_asm_mb();
		ptr[0x1e] = 0x5e;
		stress_asm_mb();
		ptr[0x21] = 0x31;
		stress_asm_mb();
		ptr[0x1f] = 0x5f;
		stress_asm_mb();
		ptr[0x20] = 0x30;
		stress_asm_mb();
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);

	for (ptr = (uint8_t *)buf; ptr < (uint8_t *)buf_end; ptr += 64) {
		bit_errors += (ptr[0x00] != 0xa0);
		bit_errors += (ptr[0x3f] != 0xcf);
		bit_errors += (ptr[0x01] != 0xa1);
		bit_errors += (ptr[0x3e] != 0xce);
		bit_errors += (ptr[0x02] != 0xa2);
		bit_errors += (ptr[0x3d] != 0xcd);
		bit_errors += (ptr[0x03] != 0xa3);
		bit_errors += (ptr[0x3c] != 0xcc);
		bit_errors += (ptr[0x04] != 0xa4);
		bit_errors += (ptr[0x3b] != 0xcb);
		bit_errors += (ptr[0x05] != 0xa5);
		bit_errors += (ptr[0x3a] != 0xca);
		bit_errors += (ptr[0x06] != 0xa6);
		bit_errors += (ptr[0x39] != 0xc9);
		bit_errors += (ptr[0x07] != 0xa7);
		bit_errors += (ptr[0x38] != 0xc8);
		bit_errors += (ptr[0x08] != 0xa8);
		bit_errors += (ptr[0x37] != 0xc7);
		bit_errors += (ptr[0x09] != 0xa9);
		bit_errors += (ptr[0x36] != 0xc6);
		bit_errors += (ptr[0x0a] != 0xaa);
		bit_errors += (ptr[0x35] != 0xc5);
		bit_errors += (ptr[0x0b] != 0xab);
		bit_errors += (ptr[0x34] != 0xc4);
		bit_errors += (ptr[0x0c] != 0xac);
		bit_errors += (ptr[0x33] != 0xc3);
		bit_errors += (ptr[0x0d] != 0xad);
		bit_errors += (ptr[0x32] != 0xc2);
		bit_errors += (ptr[0x0e] != 0xae);
		bit_errors += (ptr[0x31] != 0xc1);
		bit_errors += (ptr[0x0f] != 0xaf);
		bit_errors += (ptr[0x30] != 0xc0);
		bit_errors += (ptr[0x10] != 0x50);
		bit_errors += (ptr[0x2f] != 0x3f);
		bit_errors += (ptr[0x11] != 0x51);
		bit_errors += (ptr[0x2e] != 0x3e);
		bit_errors += (ptr[0x12] != 0x52);
		bit_errors += (ptr[0x2d] != 0x3d);
		bit_errors += (ptr[0x13] != 0x53);
		bit_errors += (ptr[0x2c] != 0x3c);
		bit_errors += (ptr[0x14] != 0x54);
		bit_errors += (ptr[0x2b] != 0x3b);
		bit_errors += (ptr[0x15] != 0x55);
		bit_errors += (ptr[0x2a] != 0x3a);
		bit_errors += (ptr[0x16] != 0x56);
		bit_errors += (ptr[0x29] != 0x39);
		bit_errors += (ptr[0x17] != 0x57);
		bit_errors += (ptr[0x28] != 0x38);
		bit_errors += (ptr[0x18] != 0x58);
		bit_errors += (ptr[0x27] != 0x37);
		bit_errors += (ptr[0x19] != 0x59);
		bit_errors += (ptr[0x25] != 0x35);
		bit_errors += (ptr[0x1a] != 0x5a);
		bit_errors += (ptr[0x26] != 0x36);
		bit_errors += (ptr[0x1b] != 0x5b);
		bit_errors += (ptr[0x24] != 0x34);
		bit_errors += (ptr[0x1c] != 0x5c);
		bit_errors += (ptr[0x23] != 0x33);
		bit_errors += (ptr[0x1d] != 0x5d);
		bit_errors += (ptr[0x22] != 0x32);
		bit_errors += (ptr[0x1e] != 0x5e);
		bit_errors += (ptr[0x21] != 0x31);
		bit_errors += (ptr[0x1f] != 0x5f);
		bit_errors += (ptr[0x20] != 0x30);
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	stress_vm_check("cache-stripe", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_cache_lines()
 *	work through memory in cache line steps touching one byte per
 * 	cache line
 */
static size_t TARGET_CLONES stress_vm_cache_lines(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint8_t *ptr;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);
	uint8_t i;
	static size_t offset = 0;

	for (i = 0, ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += stress_vm_cache_line_size) {
		*ptr = i++;
		stress_asm_mb();
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	c += sz / stress_vm_cache_line_size;
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto abort;
	if (UNLIKELY(!stress_continue_flag()))
		goto abort;

	for (i = 0, ptr = (uint8_t *)buf + offset; ptr < (uint8_t *)buf_end; ptr += stress_vm_cache_line_size) {
		bit_errors += (*ptr != i++);
	}
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("cache-lines", bit_errors);
abort:
	stress_bogo_set(args, c);

	offset = (offset + 1) & 0x3f;

	return bit_errors;
}

#if defined(HAVE_NT_STORE128) &&	\
    defined(HAVE_INT128_T)

#if defined(HAVE_NT_LOAD128)
#define STRESS_NT_LOAD128(ptr)	stress_nt_load128((ptr))
#else
#define STRESS_NT_LOAD128(ptr)	(*(ptr))
#endif

/*
 *  stress_vm_wrrd128nt()
 *	work through memory in 128 bit steps performing non-temporal
 *	writes and if possible reads
 */
static size_t TARGET_CLONES stress_vm_wrrd128nt(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);
	register __uint128_t val;
	register __uint128_t *ptr128;
	register __uint128_t *buf128 = (__uint128_t *)buf;
	register const __uint128_t *buf_end128 = (__uint128_t *)buf_end;

	for (val = 0, ptr128 = buf128; ptr128 < buf_end128; ptr128 += 4) {
		/* Write 128 bits x 4 times to hammer the memory */
		stress_nt_store128(ptr128 + 0x00, val);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x01, val + 1);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x02, val + 2);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x03, val + 3);
		stress_asm_mb();

		stress_nt_store128(ptr128 + 0x00, val);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x01, val + 1);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x02, val + 2);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x03, val + 3);
		stress_asm_mb();

		stress_nt_store128(ptr128 + 0x00, val);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x01, val + 1);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x02, val + 2);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x03, val + 3);
		stress_asm_mb();

		stress_nt_store128(ptr128 + 0x00, val);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x01, val + 1);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x02, val + 2);
		stress_asm_mb();
		stress_nt_store128(ptr128 + 0x03, val + 3);
		stress_asm_mb();
		val++;
		c++;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	if (UNLIKELY(max_ops && (c >= max_ops)))
		goto abort;
	if (UNLIKELY(!stress_continue_flag()))
		goto abort;

	for (val = 0, ptr128 = buf128; ptr128 < buf_end128; ptr128 += 4) {
		register __uint128_t tmp;

		tmp = STRESS_NT_LOAD128(ptr128 + 0x0);
		bit_errors += (tmp != (val + 0));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x1);
		bit_errors += (tmp != (val + 1));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x2);
		bit_errors += (tmp != (val + 2));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x3);
		bit_errors += (tmp != (val + 3));

		tmp = STRESS_NT_LOAD128(ptr128 + 0x0);
		bit_errors += (tmp != (val + 0));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x1);
		bit_errors += (tmp != (val + 1));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x2);
		bit_errors += (tmp != (val + 2));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x3);
		bit_errors += (tmp != (val + 3));

		tmp = STRESS_NT_LOAD128(ptr128 + 0x0);
		bit_errors += (tmp != (val + 0));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x1);
		bit_errors += (tmp != (val + 1));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x2);
		bit_errors += (tmp != (val + 2));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x3);
		bit_errors += (tmp != (val + 3));

		tmp = STRESS_NT_LOAD128(ptr128 + 0x0);
		bit_errors += (tmp != (val + 0));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x1);
		bit_errors += (tmp != (val + 1));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x2);
		bit_errors += (tmp != (val + 2));
		tmp = STRESS_NT_LOAD128(ptr128 + 0x3);
		bit_errors += (tmp != (val + 3));
		val++;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	inject_random_bit_errors(buf, sz);
	stress_vm_check("wrrd128nt", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}
#endif

/*
 *  stress_vm_fwdrev()
 *	write forwards even bytes and reverse odd bytes
 */
static size_t TARGET_CLONES stress_vm_fwdrev(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	size_t bit_errors = 0;
	register uint8_t *fwdptr, *revptr;
	register uint64_t c = stress_bogo_get(args);
	register const uint32_t rnd = stress_mwc32();

	(void)sz;

	for (fwdptr = (uint8_t *)buf, revptr = (uint8_t *)buf_end; fwdptr < (uint8_t *)buf_end; ) {
		*(fwdptr + 0) = (rnd >> 0x00) & 0xff;
		stress_asm_mb();
		*(revptr - 1) = (rnd >> 0x08) & 0xff;
		stress_asm_mb();
		*(fwdptr + 2) = (rnd >> 0x10) & 0xff;
		stress_asm_mb();
		*(revptr - 3) = (rnd >> 0x18) & 0xff;
		stress_asm_mb();
		*(fwdptr + 4) = (rnd >> 0x00) & 0xff;
		stress_asm_mb();
		*(revptr - 5) = (rnd >> 0x08) & 0xff;
		stress_asm_mb();
		*(fwdptr + 6) = (rnd >> 0x10) & 0xff;
		stress_asm_mb();
		*(revptr - 7) = (rnd >> 0x18) & 0xff;
		stress_asm_mb();
		*(fwdptr + 8) = (rnd >> 0x00) & 0xff;
		stress_asm_mb();
		*(revptr - 9) = (rnd >> 0x08) & 0xff;
		stress_asm_mb();
		*(fwdptr + 10) = (rnd >> 0x10) & 0xff;
		stress_asm_mb();
		*(revptr - 11) = (rnd >> 0x18) & 0xff;
		stress_asm_mb();
		*(fwdptr + 12) = (rnd >> 0x00) & 0xff;
		stress_asm_mb();
		*(revptr - 13) = (rnd >> 0x08) & 0xff;
		stress_asm_mb();
		*(fwdptr + 14) = (rnd >> 0x10) & 0xff;
		stress_asm_mb();
		*(revptr - 15) = (rnd >> 0x18) & 0xff;
		stress_asm_mb();
		fwdptr += 16;
		revptr -= 16;
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);

	for (fwdptr = (uint8_t *)buf, revptr = (uint8_t *)buf_end; fwdptr < (uint8_t *)buf_end; ) {
		bit_errors += (*(fwdptr + 0) != ((rnd >> 0x00) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 1) != ((rnd >> 0x08) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 2) != ((rnd >> 0x10) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 3) != ((rnd >> 0x18) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 4) != ((rnd >> 0x00) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 5) != ((rnd >> 0x08) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 6) != ((rnd >> 0x10) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 7) != ((rnd >> 0x18) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 8) != ((rnd >> 0x00) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 9) != ((rnd >> 0x08) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 10) != ((rnd >> 0x10) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 11) != ((rnd >> 0x18) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 12) != ((rnd >> 0x00) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 13) != ((rnd >> 0x08) & 0xff));
		stress_asm_mb();
		bit_errors += (*(fwdptr + 14) != ((rnd >> 0x10) & 0xff));
		stress_asm_mb();
		bit_errors += (*(revptr - 15) != ((rnd >> 0x18) & 0xff));
		stress_asm_mb();
		fwdptr += 16;
		revptr -= 16;
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("fwdrev", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

/*
 *  stress_vm_lfsr32()
 *	sequentially set all memory to 32 bit random values
 *	check if they are still set correctly.
 */
static size_t stress_vm_lfsr32(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint32_t *ptr;
	register uint64_t c = stress_bogo_get(args);
	size_t bit_errors = 0;
	const size_t chunk_sz = sizeof(*ptr) * 8;
	register uint32_t lfsr = 0xf63acb01;

	(void)buf_end;

	for (lfsr = 0xf63acb01, ptr = (uint32_t *)buf; ptr < (uint32_t *)buf_end; ptr += chunk_sz) {
		*(ptr + 0) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 1) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 2) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 3) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 4) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 5) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 6) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		*(ptr + 7) = lfsr;
		stress_asm_mb();
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			goto abort;
		if (UNLIKELY(!stress_continue_flag()))
			goto abort;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (lfsr = 0xf63acb01, ptr = (uint32_t *)buf; ptr < (uint32_t *)buf_end; ptr += chunk_sz) {
		bit_errors += stress_vm_count_bits(*(ptr + 0) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 1) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 2) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 3) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 4) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 5) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 6) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		bit_errors += stress_vm_count_bits(*(ptr + 7) ^ lfsr);
		lfsr = (lfsr >> 1) ^ (unsigned int)(-(lfsr & 1u) & 0xd0000001U);
		if (UNLIKELY(!stress_continue_flag()))
			break;
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("lfsr32", bit_errors);
abort:
	stress_bogo_set(args, c);

	return bit_errors;
}

#define STRESS_VM_CHECKERBOARD_SWAP(p1, p2)	\
do {						\
		register uint64_t tmp;		\
						\
		tmp = *(p1);			\
		stress_asm_mb();			\
		*(p1) = *(p2);			\
		stress_asm_mb();			\
		*(p2) = tmp;			\
		stress_asm_mb();			\
} while (0);

/*
 *  stress_vm_checkerboard()
 *	fill adjacent bytes with alternative bit patterns
 */
static size_t TARGET_CLONES stress_vm_checkerboard(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	register uint64_t *ptr;
	size_t bit_errors = 0;
	register uint64_t c = stress_bogo_get(args);

	const uint64_t v0 = 0x5555aaaa5555aaaaULL;
	const uint64_t v1 = 0xaaaa5555aaaa5555ULL;
	const uint64_t v2 = 0x5a5a5a5a5a5a5a5aULL;
	const uint64_t v3 = 0xa5a5a5a5a5a5a5a5ULL;
	const uint64_t v4 = 0x55aa55aa55aa55aaULL;
	const uint64_t v5 = 0xaa55aa55aa55aa55ULL;
	const uint64_t v6 = 0x5a5a5a5a5a5a5a5aULL;
	const uint64_t v7 = 0xa5a5a5a5a5a5a5a5ULL;

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		if (UNLIKELY(!stress_continue_flag()))
			return 0;
		*(ptr + 0) = v1;
		stress_asm_mb();
		*(ptr + 1) = v0;
		stress_asm_mb();
		*(ptr + 2) = v3;
		stress_asm_mb();
		*(ptr + 3) = v2;
		stress_asm_mb();
		*(ptr + 4) = v5;
		stress_asm_mb();
		*(ptr + 5) = v4;
		stress_asm_mb();
		*(ptr + 6) = v7;
		stress_asm_mb();
		*(ptr + 7) = v6;
		stress_asm_mb();

		*(ptr + 0) = v0;
		stress_asm_mb();
		*(ptr + 1) = v1;
		stress_asm_mb();
		*(ptr + 2) = v2;
		stress_asm_mb();
		*(ptr + 3) = v3;
		stress_asm_mb();
		*(ptr + 4) = v4;
		stress_asm_mb();
		*(ptr + 5) = v5;
		stress_asm_mb();
		*(ptr + 6) = v6;
		stress_asm_mb();
		*(ptr + 7) = v7;
		stress_asm_mb();
	}

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		if (UNLIKELY(!stress_continue_flag()))
			break;

		STRESS_VM_CHECKERBOARD_SWAP(ptr + 0, ptr + 1);
		stress_asm_mb();
		STRESS_VM_CHECKERBOARD_SWAP(ptr + 2, ptr + 3);
		stress_asm_mb();
		STRESS_VM_CHECKERBOARD_SWAP(ptr + 4, ptr + 5);
		stress_asm_mb();
		STRESS_VM_CHECKERBOARD_SWAP(ptr + 6, ptr + 7);
	}
	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	(void)stress_mincore_touch_pages(buf, sz);
	inject_random_bit_errors(buf, sz);

	for (ptr = (uint64_t *)buf; ptr < (uint64_t *)buf_end; ptr += 8) {
		if (UNLIKELY(!stress_continue_flag()))
			return 0;
		bit_errors += (*(ptr + 0) != v1);
		bit_errors += (*(ptr + 1) != v0);
		bit_errors += (*(ptr + 2) != v3);
		bit_errors += (*(ptr + 3) != v2);
		bit_errors += (*(ptr + 4) != v5);
		bit_errors += (*(ptr + 5) != v4);
		bit_errors += (*(ptr + 6) != v7);
		bit_errors += (*(ptr + 7) != v6);
		c++;
		if (UNLIKELY(max_ops && (c >= max_ops)))
			break;
	}

	if (vm_flush)
		stress_cpu_data_cache_flush(buf, sz);
	stress_vm_check("checkerboard", bit_errors);
	stress_bogo_set(args, c);

	return bit_errors;
}

static size_t stress_vm_all(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops);

static const stress_vm_method_info_t vm_methods[] = {
	{ "all",		stress_vm_all },
	{ "cache-lines",	stress_vm_cache_lines },
	{ "cache-stripe",	stress_vm_cache_stripe },
	{ "checkerboard",	stress_vm_checkerboard },
	{ "flip",		stress_vm_flip },
	{ "fwdrev",		stress_vm_fwdrev },
	{ "galpat-0",		stress_vm_galpat_zero },
	{ "galpat-1",		stress_vm_galpat_one },
	{ "gray",		stress_vm_gray },
	{ "grayflip",		stress_vm_grayflip },
	{ "incdec",		stress_vm_incdec },
	{ "inc-nybble",		stress_vm_inc_nybble },
	{ "lfsr32",		stress_vm_lfsr32 },
	{ "modulo-x",		stress_vm_modulo_x },
	{ "move-inv",		stress_vm_moving_inversion },
	{ "mscan",		stress_vm_mscan },
	{ "one-zero",		stress_vm_one_zero },
	{ "prime-0",		stress_vm_prime_zero },
	{ "prime-1",		stress_vm_prime_one },
	{ "prime-gray-0",	stress_vm_prime_gray_zero },
	{ "prime-gray-1",	stress_vm_prime_gray_one },
	{ "prime-incdec",	stress_vm_prime_incdec },
	{ "rand-set",		stress_vm_rand_set },
	{ "rand-sum",		stress_vm_rand_sum },
	{ "read64",		stress_vm_read64 },
	{ "ror",		stress_vm_ror },
	{ "rowhammer",		stress_vm_rowhammer },
	{ "swap",		stress_vm_swap },
	{ "walk-0a",		stress_vm_walking_zero_addr },
	{ "walk-0d",		stress_vm_walking_zero_data },
	{ "walk-1a",		stress_vm_walking_one_addr },
	{ "walk-1d",		stress_vm_walking_one_data },
	{ "walk-flush",		stress_vm_walking_flush_data },
	{ "write64",		stress_vm_write64 },
#if defined(HAVE_ASM_X86_MOVDIRI) &&	\
    defined(STRESS_ARCH_X86_64)
	{ "write64ds",		stress_vm_write64ds },
#endif
#if defined(HAVE_NT_STORE64)
	{ "write64nt",		stress_vm_write64nt },
#endif
#if defined(HAVE_VECMATH)
	{ "write1024v",		stress_vm_write1024v },
#endif
#if defined(HAVE_NT_STORE128)
	{ "wrrd128nt",		stress_vm_wrrd128nt },
#endif
	{ "zero-one",		stress_vm_zero_one },
};

/*
 *  stress_vm_all()
 *	work through all vm stressors sequentially
 */
static size_t stress_vm_all(
	void *buf,
	void *buf_end,
	const size_t sz,
	stress_args_t *args,
	const uint64_t max_ops)
{
	static size_t i = 1;
	size_t bit_errors = 0;

	bit_errors = vm_methods[i].func(buf, buf_end, sz, args, max_ops);
	i++;
	if (UNLIKELY(i >= SIZEOF_ARRAY(vm_methods)))
		i = 1;

	return bit_errors;
}

#if defined(MAP_LOCKED) ||	\
    defined(MAP_POPULATE)
static void stress_vm_flags(const char *opt, int *vm_flags, const int bitmask)
{
	bool flag = false;

	(void)stress_get_setting(opt, &flag);
	if (flag)
		*vm_flags |= bitmask;
}
#endif

static int stress_vm_child(stress_args_t *args, void *ctxt)
{
	stress_vm_context_t *context = (stress_vm_context_t *)ctxt;
	const stress_vm_func func = context->vm_method->func;
	const size_t page_size = args->page_size;
	size_t buf_sz = context->vm_bytes & ~(page_size - 1);
	const uint64_t max_ops = args->bogo.max_ops << VM_BOGO_SHIFT;
	uint64_t vm_hang = DEFAULT_VM_HANG;
	double t_start, duration, rate;
	void *buf = NULL, *buf_end = NULL;
	int no_mem_retries = 0;
	int vm_flags = 0;                      /* VM mmap flags */
	size_t vm_madvise = 0;
	int advice = -1;
	int rc = EXIT_SUCCESS;
	bool vm_keep = false;

	stress_catch_sigill();

	(void)stress_get_setting("vm-flush", &vm_flush);
	if (g_opt_flags & OPT_FLAGS_AGGRESSIVE)
		vm_flush = true;
	(void)stress_get_setting("vm-hang", &vm_hang);
	(void)stress_get_setting("vm-keep", &vm_keep);
#if defined(MAP_LOCKED)
	stress_vm_flags("vm-locked", &vm_flags, MAP_LOCKED);
#endif
#if defined(MAP_POPULATE)
	stress_vm_flags("vm-populate", &vm_flags, MAP_POPULATE);
#endif
	(void)stress_get_setting("vm-flags", &vm_flags);
	if (stress_get_setting("vm-madvise", &vm_madvise))
		advice = vm_madvise_info[vm_madvise].advice;

	t_start = stress_time_now();
	do {
		if (!vm_keep || (buf == NULL)) {
			if (UNLIKELY(!stress_continue_flag()))
				return EXIT_SUCCESS;
			if (UNLIKELY(((g_opt_flags & OPT_FLAGS_OOM_AVOID) && stress_low_memory(buf_sz)))) {
				buf = MAP_FAILED;
				errno = ENOMEM;
			} else {
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_NONE)
				/*
				 *   allocate buffer + one trailing page
				 *   so the last page can be marked PROT_NONE later
				 *   to catch any buffer over-runs.
				 */
				buf = (uint8_t *)mmap(NULL, buf_sz + page_size,
#else
				buf = (uint8_t *)mmap(NULL, buf_sz,
#endif
					PROT_READ | PROT_WRITE,
					MAP_PRIVATE | MAP_ANONYMOUS |
					vm_flags, -1, 0);
			}
			if (UNLIKELY(buf == MAP_FAILED)) {
				buf = NULL;
				no_mem_retries++;
				if (no_mem_retries >= NO_MEM_RETRIES_MAX) {
					char str[32];

					/* shrink a bit and retry */
					buf_sz = ((buf_sz / 16) * 15) & ~(page_size - 1);
					no_mem_retries = 0;
					if (buf_sz < page_size) {
						(void)stress_uint64_to_str(str, sizeof(str), (uint64_t)buf_sz, 1, true);
						pr_inf_skip("%s: gave up trying to mmap %s after many attempts, "
							"errno=%d (%s), skipping stressor\n",
							args->name, str, errno, strerror(errno));
						rc = EXIT_NO_RESOURCE;
						break;
					}
				}
				(void)shim_usleep(10000);
				continue;	/* Try again */
			}
			buf_end = (void *)((uint8_t *)buf + buf_sz);
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_NONE)
			/*
			 * page after end of buffer is not readable or writable
			 * to catch any buffer overruns
			 */
			(void)mprotect(buf_end, page_size, PROT_NONE);
#endif

			if (advice < 0)
				(void)stress_madvise_randomize(buf, buf_sz);
			else
				(void)shim_madvise(buf, buf_sz, advice);
#if defined(HAVE_LINUX_MEMPOLICY_H)
			if (UNLIKELY(context->vm_numa))
				stress_numa_randomize_pages(args, context->numa_nodes, context->numa_mask, buf, buf_sz, page_size);
#endif
		}

		no_mem_retries = 0;
		(void)stress_mincore_touch_pages(buf, buf_sz);
		*(context->bit_error_count) += func(buf, buf_end, buf_sz, args, max_ops);

		if (vm_hang == 0) {
			while (stress_continue_vm(args)) {
				(void)sleep(3600);
			}
		} else if (vm_hang != DEFAULT_VM_HANG) {
			(void)sleep((unsigned int)vm_hang);
		}

		if (!vm_keep) {
			(void)stress_madvise_randomize(buf, buf_sz);
#if defined(HAVE_MPROTECT) &&	\
    defined(PROT_NONE)
			(void)stress_munmap_force(buf, buf_sz + page_size);
#else
			(void)stress_munmap_force(buf, buf_sz);
#endif
		}
	} while (stress_continue_vm(args));

	duration = stress_time_now() - t_start;

	if (vm_keep && (buf != NULL)) {
#if defined(HAVE_MPROTECT) && 	\
    defined(PROT_NONE)
		(void)stress_munmap_force(buf, buf_sz + page_size);
#else
		(void)stress_munmap_force(buf, buf_sz);
#endif
	}

	rate = (duration > 0.0) ? (stress_bogo_get(args) >> VM_BOGO_SHIFT) / duration : 0.0;
	pr_dbg("%s: %.2f bogo-ops per sec\n", args->name, rate);

	return rc;
}

/*
 *  stress_vm_get_cache_line_size()
 *	determine size of a cache line, default to 64 if information
 *	is not available
 */
static void stress_vm_get_cache_line_size(void)
{
#if defined(__linux__)
        stress_cpu_cache_cpus_t *cpu_caches;
        stress_cpu_cache_t *cache;

	stress_vm_cache_line_size = 64;	/* Default guess */

	cpu_caches = stress_cpu_cache_get_all_details();
	if (!cpu_caches)
		return;
	cache = stress_cpu_cache_get(cpu_caches, 1);
	if (cache && cache->line_size)
		stress_vm_cache_line_size = (size_t)cache->line_size;

	stress_free_cpu_caches(cpu_caches);
#else
	stress_vm_cache_line_size = 64;	/* Default guess */
#endif
}

/*
 *  stress_vm()
 *	stress virtual memory
 */
static int stress_vm(stress_args_t *args)
{
	uint64_t tmp_counter;
	const size_t page_size = args->page_size;
	size_t retries;
	int err = 0, ret = EXIT_SUCCESS;
	size_t vm_method = 0;
	size_t vm_total = DEFAULT_VM_BYTES;
	stress_vm_context_t context;

	(void)shim_memset(&context, 0, sizeof(context));
	stress_vm_get_cache_line_size();

	(void)stress_get_setting("vm-numa", &context.vm_numa);
	if (context.vm_numa) {
#if defined(HAVE_LINUX_MEMPOLICY_H)
		stress_numa_mask_and_node_alloc(args, &context.numa_nodes,
						&context.numa_mask, "--vm-numa",
						&context.vm_numa);
#else
		if (stress_instance_zero(args))
			pr_inf("%s: --vm-numa selected but not supported by this system, disabling option\n",
				args->name);
		context.vm_numa = false;
#endif
	}
	context.bit_error_count = MAP_FAILED;

	(void)stress_get_setting("vm-method", &vm_method);
	context.vm_method = &vm_methods[vm_method];

	if (!stress_get_setting("vm-bytes", &vm_total)) {
		if (g_opt_flags & OPT_FLAGS_MAXIMIZE)
			vm_total = MAX_32;
		if (g_opt_flags & OPT_FLAGS_MINIMIZE)
			vm_total = MIN_VM_BYTES;
	}
	context.vm_bytes = vm_total / args->instances;
	if (context.vm_bytes < MIN_VM_BYTES) {
		context.vm_bytes = MIN_VM_BYTES;
		vm_total = context.vm_bytes * args->instances;
	}
	if (context.vm_bytes < page_size) {
		context.vm_bytes = MIN_VM_BYTES;
		vm_total = context.vm_bytes * args->instances;
	}

	if (stress_instance_zero(args)) {
		pr_dbg("%s: using method '%s'\n", args->name, context.vm_method->name);
		stress_usage_bytes(args, context.vm_bytes, vm_total);
	}

	for (retries = 0; LIKELY((retries < 100) && stress_continue_flag()); retries++) {
		context.bit_error_count = (uint64_t *)
			stress_mmap_populate(NULL, page_size,
				PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		err = errno;
		if (context.bit_error_count != MAP_FAILED)
			break;
		(void)shim_usleep(100);
	}

	/* Cannot allocate a single page for bit error counter */
	if (context.bit_error_count == MAP_FAILED) {
		if (LIKELY(stress_continue_flag())) {
			pr_err("%s: could not mmap bit error counter: "
				"retry count=%zu, errno=%d (%s)\n",
				args->name, retries, err, strerror(err));
		}
#if defined(HAVE_LINUX_MEMPOLICY_H)
		if (context.numa_mask)
			stress_numa_mask_free(context.numa_mask);
		if (context.numa_nodes)
			stress_numa_mask_free(context.numa_nodes);
#endif
		return EXIT_NO_RESOURCE;
	}
	stress_set_vma_anon_name(context.bit_error_count, page_size, "bit-error-count");

	*context.bit_error_count = 0ULL;

	stress_set_proc_state(args->name, STRESS_STATE_SYNC_WAIT);
	stress_sync_start_wait(args);
	stress_set_proc_state(args->name, STRESS_STATE_RUN);

	ret = stress_oomable_child(args, &context, stress_vm_child, STRESS_OOMABLE_NORMAL);

#if defined(MS_SYNC)
	(void)shim_msync(context.bit_error_count, page_size, MS_SYNC);
#endif
	if (*context.bit_error_count > 0) {
		pr_fail("%s: detected %" PRIu64 " bit errors while "
			"stressing memory\n",
			args->name, *context.bit_error_count);
		ret = EXIT_FAILURE;
	}

	stress_set_proc_state(args->name, STRESS_STATE_DEINIT);
	(void)munmap((void *)context.bit_error_count, page_size);

#if defined(HAVE_LINUX_MEMPOLICY_H)
	if (context.numa_mask)
		stress_numa_mask_free(context.numa_mask);
	if (context.numa_nodes)
		stress_numa_mask_free(context.numa_nodes);
#endif
	tmp_counter = stress_bogo_get(args) >> VM_BOGO_SHIFT;
	stress_bogo_set(args, tmp_counter);

	return ret;
}

static const char *stress_vm_madvise(const size_t i)
{
	return (i < SIZEOF_ARRAY(vm_madvise_info)) ? vm_madvise_info[i].name : NULL;
}

static const char *stress_vm_method(const size_t i)
{
	return (i < SIZEOF_ARRAY(vm_methods)) ? vm_methods[i].name : NULL;
}

static const stress_opt_t opts[] = {
	{ OPT_vm_bytes,    "vm-bytes",    TYPE_ID_SIZE_T_BYTES_VM, MIN_VM_BYTES, MAX_VM_BYTES, NULL },
	{ OPT_vm_flush,	   "vm-flush",	  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vm_hang,     "vm-hang",     TYPE_ID_UINT64, MIN_VM_HANG, MAX_VM_HANG, NULL },
	{ OPT_vm_keep,     "vm-keep",     TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vm_locked,   "vm-locked",   TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vm_madvise,  "vm-madvise",  TYPE_ID_SIZE_T_METHOD, 0, 0, stress_vm_madvise },
	{ OPT_vm_method,   "vm-method",   TYPE_ID_SIZE_T_METHOD, 0, 0, stress_vm_method },
	{ OPT_vm_numa,	   "vm-numa",	  TYPE_ID_BOOL, 0, 1, NULL },
	{ OPT_vm_populate, "vm-populate", TYPE_ID_BOOL, 0, 1, NULL },
	END_OPT,
};

const stressor_info_t stress_vm_info = {
	.stressor = stress_vm,
	.classifier = CLASS_VM | CLASS_MEMORY | CLASS_OS,
	.opts = opts,
	.verify = VERIFY_OPTIONAL,
	.help = help
};
